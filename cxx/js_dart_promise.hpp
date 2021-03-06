/*
 * @Description: 
 * @Author: ekibun
 * @Date: 2020-08-07 13:55:52
 * @LastEditors: ekibun
 * @LastEditTime: 2020-08-27 20:31:25
 */
#pragma once
#include "quickjs/quickjspp.hpp"
#include <future>
#include <string.h>

namespace std
{
  template <>
  struct hash<qjs::Value>
  {
    size_t operator()(const qjs::Value &key) const
    {
      return (size_t)JS_VALUE_GET_PTR(key.v);
    }
  };
} // namespace std

namespace qjs
{
#include "quickjs/list.h"

  static JSClassID js_dart_promise_class_id;

  typedef struct
  {
    int count;
    JSValue *argv;
  } JSOSFutureArgv;

  using JSFutureReturn = std::function<JSOSFutureArgv(JSContext *)>;
  using DartChannel = std::function<std::promise<JSFutureReturn> *(std::string, Value)>;

  typedef struct
  {
    struct list_head link;
    std::promise<JSFutureReturn> *promise;
    std::shared_future<JSFutureReturn> future;
    JSValue resolve;
    JSValue reject;
  } JSOSFuture;

  typedef struct
  {
    struct list_head link;
    JSValue ref;
  } JSOSRef;

  typedef struct JSThreadState
  {
    struct list_head os_future;
    struct list_head os_ref;
    DartChannel channel;
  } JSThreadState;

  JSModuleDef *js_module_loader(
      JSContext *ctx,
      const char *module_name, void *opaque)
  {
    JSRuntime *rt = JS_GetRuntime(ctx);
    JSThreadState *ts = (JSThreadState *)JS_GetRuntimeOpaque(rt);
    auto promise = ts->channel("__dart_load_module__", Value{ctx, JS_NewString(ctx, module_name)});
    JSOSFutureArgv argv = promise->get_future().get()(ctx);
    if (argv.count > 0)
    {
      const char *str = JS_ToCString(ctx, argv.argv[0]);
      JSValue func_val = JS_Eval(ctx, str, strlen(str), module_name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
      JS_FreeCString(ctx, str);
      JS_FreeValue(ctx, argv.argv[0]);
      if (JS_IsException(func_val))
        return NULL;
      /* the module is already referenced, so we must free it */
      JSModuleDef *m = (JSModuleDef *)JS_VALUE_GET_PTR(func_val);
      JS_FreeValue(ctx, func_val);
      return m;
    }
    else
    {
      JS_Throw(ctx, argv.argv[0]);
      return NULL;
    }
  }

  JSValue js_add_ref(Value val)
  {
    JSRuntime *rt = JS_GetRuntime(val.ctx);
    JSThreadState *ts = (JSThreadState *)JS_GetRuntimeOpaque(rt);
    JSOSRef *th;
    th = (JSOSRef *)js_mallocz(val.ctx, sizeof(*th));
    th->ref = JS_DupValue(val.ctx, val.v);
    list_add_tail(&th->link, &ts->os_ref);
    return th->ref;
  }

  static JSValue js_add_future(Value resolve, Value reject, std::promise<JSFutureReturn> *promise)
  {
    JSRuntime *rt = JS_GetRuntime(resolve.ctx);
    JSThreadState *ts = (JSThreadState *)JS_GetRuntimeOpaque(rt);
    JSValueConst jsResolve, jsReject;
    JSOSFuture *th;
    JSValue obj;

    jsResolve = resolve.v;
    if (!JS_IsFunction(resolve.ctx, jsResolve))
      return JS_ThrowTypeError(resolve.ctx, "resolve not a function");
    jsReject = reject.v;
    if (!JS_IsFunction(reject.ctx, jsReject))
      return JS_ThrowTypeError(reject.ctx, "reject not a function");
    obj = JS_NewObjectClass(resolve.ctx, js_dart_promise_class_id);
    if (JS_IsException(obj))
      return obj;
    th = (JSOSFuture *)js_mallocz(resolve.ctx, sizeof(*th));
    if (!th)
    {
      JS_FreeValue(resolve.ctx, obj);
      return JS_EXCEPTION;
    }
    th->promise = promise;
    th->future = promise->get_future();
    th->resolve = JS_DupValue(resolve.ctx, jsResolve);
    th->reject = JS_DupValue(reject.ctx, jsReject);
    list_add_tail(&th->link, &ts->os_future);
    JS_SetOpaque(obj, th);
    return obj;
  }

  JSValue js_dart_future(Value resolve, Value reject, std::string name, Value args)
  {
    JSRuntime *rt = JS_GetRuntime(resolve.ctx);
    JSThreadState *ts = (JSThreadState *)JS_GetRuntimeOpaque(rt);
    return js_add_future(resolve, reject, ts->channel(name, args));
  }

  static void unlink_ref(JSRuntime *rt, JSOSRef *th)
  {
    if (th->link.prev)
    {
      list_del(&th->link);
      th->link.prev = th->link.next = NULL;
    }
  }

  static void free_ref(JSRuntime *rt, JSOSRef *th)
  {
    JS_FreeValueRT(rt, th->ref);
    js_free_rt(rt, th);
  }

  static void unlink_future(JSRuntime *rt, JSOSFuture *th)
  {
    if (th->link.prev)
    {
      list_del(&th->link);
      th->link.prev = th->link.next = NULL;
    }
  }

  static void free_future(JSRuntime *rt, JSOSFuture *th)
  {
    JS_FreeValueRT(rt, th->resolve);
    JS_FreeValueRT(rt, th->reject);
    js_free_rt(rt, th);
  }

  void js_init_handlers(JSRuntime *rt, DartChannel channel)
  {
    JSThreadState *ts = (JSThreadState *)malloc(sizeof(*ts));
    if (!ts)
    {
      fprintf(stderr, "Could not allocate memory for the worker");
      exit(1);
    }
    memset(ts, 0, sizeof(*ts));
    init_list_head(&ts->os_future);
    init_list_head(&ts->os_ref);
    ts->channel = channel;

    JS_SetRuntimeOpaque(rt, ts);
  }

  void js_free_handlers(JSRuntime *rt)
  {
    JSThreadState *ts = (JSThreadState *)JS_GetRuntimeOpaque(rt);
    struct list_head *el, *el1;

    list_for_each_safe(el, el1, &ts->os_future)
    {
      JSOSFuture *th = list_entry(el, JSOSFuture, link);
      th->future.get();
      delete th->promise;
      unlink_future(rt, th);
      free_future(rt, th);
    }
    list_for_each_safe(el, el1, &ts->os_ref)
    {
      JSOSRef *th = list_entry(el, JSOSRef, link);
      unlink_ref(rt, th);
      free_ref(rt, th);
    }
    ts->channel = nullptr;
    free(ts);
    JS_SetRuntimeOpaque(rt, NULL); /* fail safe */
  }

  static JSValue call_handler(JSContext *ctx, JSValueConst func, int count, JSValue *argv)
  {
    JSValue ret, func1;
    /* 'func' might be destroyed when calling itself (if it frees the
        handler), so must take extra care */
    func1 = JS_DupValue(ctx, func);
    ret = JS_Call(ctx, func1, JS_UNDEFINED, count, argv);
    JS_FreeValue(ctx, func1);
    for (int i = 0; i < count; ++i)
      JS_FreeValue(ctx, argv[i]);
    return ret;
  }

  static int js_dart_poll(JSContext *ctx)
  {
    JSRuntime *rt = JS_GetRuntime(ctx);
    JSThreadState *ts = (JSThreadState *)JS_GetRuntimeOpaque(rt);
    struct list_head *el;

    /* XXX: handle signals if useful */

    if (list_empty(&ts->os_future))
      return -1; /* no more events */

    /* XXX: only timers and basic console input are supported */
    if (!list_empty(&ts->os_future))
    {
      list_for_each(el, &ts->os_future)
      {
        JSOSFuture *th = list_entry(el, JSOSFuture, link);
        auto status = th->future.wait_for(std::chrono::seconds(0));
        if (status == std::future_status::ready)
        {
          JSOSFutureArgv argv = th->future.get()(ctx);
          JSValue resolve, reject;
          /* the timer expired */
          resolve = th->resolve;
          th->resolve = JS_UNDEFINED;
          reject = th->reject;
          th->reject = JS_UNDEFINED;
          unlink_future(rt, th);
          free_future(rt, th);
          JSValue ret = call_handler(ctx, argv.count < 0 ? reject : resolve, abs(argv.count), argv.argv);
          if (qjs::JS_IsException(ret))
            throw qjs::exception{};
          JS_FreeValue(ctx, ret);
          JS_FreeValue(ctx, resolve);
          JS_FreeValue(ctx, reject);
          delete argv.argv;
          return 0;
        }
      }
    }
    return 0;
  }
} // namespace qjs
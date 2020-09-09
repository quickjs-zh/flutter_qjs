<!--
 * @Description: 
 * @Author: ekibun
 * @Date: 2020-08-08 08:16:50
 * @LastEditors: ekibun
 * @LastEditTime: 2020-08-27 21:11:55
-->
# flutter_qjs

一个为flutter开发的quickjs引擎。

## QuickJS

官方站点：https://bellard.org/quickjs/

中文站点：https://github.com/quickjs-zh/

QuickJS QQ群：**598609506**。

## 功能

这是一个使用`quickjs`为底层，为flutter开发的一个轻量级JS引擎插件。当前插件支持Windows，Linux和Android。

每个 `FlutterJs` 对象创建一个新的线程来执行js loop。

ES6模块中`import`函数已经支持可以通过dart中`setModuleHandler`来管理。

提供一个全局的`dart`函数来调用dart方法, 并且`Promise` 已经被支持，所以你可以使用`await`或者`then`来获取来自`dart`中处理的结果。dart和js直接的数据转化可以参考:

| dart | js |
| --- | --- |
| Bool | boolean |
| Int | number |
| Double | number |
| String | string |
| Uint8List/Int32List/Int64List | ArrayBuffer |
| Float64List | number[] |
| List | Array |
| Map | Object |
| Closure(List) => Future | function(....args) |

**notice:**
1. All the `Uint8List/Int32List/Int64List` sent from dart will be converted to `ArrayBuffer` without marked the size of elements, and the `ArrayBuffer` will be converted to `Uint8List`.

2. `function` can only sent from js to dart and all the arguments will be packed in a dart `List` object.

## Getting Started

1. Create a `FlutterJs` object. Make sure call `destroy` to terminate thread and release memory when you don't need it.

```dart
FlutterJs engine = FlutterJs();
// do something ...
await engine.destroy();
engine = null;
```

2. Call `setMethodHandler` to implements `dart` interaction. For example, you can use `Dio` to implements http in js:

```dart
await engine.setMethodHandler((String method, List arg) async {
  switch (method) {
    case "http":
      Response response = await Dio().get(arg[0]);
      return response.data;
    default:
      return JsMethodHandlerNotImplement();
  }
});
```

and in javascript, call `dart` function to get data:

```javascript
dart("http", "http://example.com/");
```

3. Call `setModuleHandler` to resolve js module. For example, you can use assets files as module:

```dart
await engine.setModuleHandler((String module) async {
  return await rootBundle.loadString(
      "js/" + module.replaceFirst(new RegExp(r".js$"), "") + ".js");
});
```

and in javascript, call `import` function to get module:

```javascript
import("hello").then(({default: greet}) => greet("world"));
```

4. Use `evaluate` to run js script, and try-catch is needed to capture js exception.

```dart
try {
  print(await engine.evaluate(code ?? '', "<eval>"));
} catch (e) {
  print(e.toString());
}
```

[This example](example/lib/main.dart) contains a complete demonstration on how to use this plugin.

* 2022-01-23: Blending has been reworked and blending-operations are actually blending-operations. The former blending-operations have been renamed to blending-factors.
* 2021-12-21: A parameter specyfiying the index-format was added to kinc_g5_index_buffer_init
* 2021-11-15: The input callbacks are now set using function calls like in other parts of the API.
* 2021-07-05: kinc_g4_index_buffer_init now has an additional usage-parameter, just like kinc_g4_vertex_buffer_init. Pass KINC_G4_USAGE_STATIC to retain the previous behaviour.
* 2021-05-24: The window-parameter has been removed from most of the mouse-locking functions. If you use them, just remove that parameter, too.
* 2020-09-19: When using a kincfile.js by default the Kore API isn't included anymore. It continues to be auto-included when using a korefile.js and can also be included via a kincfile by setting project.cpp to true.
* 2019-06-01: Kore is now a C project (called Kinc, short for Kore in C) with a C++ wrapper (still called Kore). For C++ everything should still work the same (in theory) but for one detail: The start procedure is now called kickstart instead of kore.
* 2018-07-08: Kore::Window and Kore::Display have been overhauled and Kore::System was simplified. Projects are now always initialized using Kore::System::init (see Tests/Empty/Sources/Shader.cpp), additional windows are created using Kore::Window::create.
* 2017-02-10: createProject and addSubProject were replaced by the simpler addProject. New code looks like this:

```
let project = new Project('Cool Name');
await project.addProject('aCoolLib');
resolve(project);
```

addProject is asynchronous, please do not forget to await it.

Kore itself is now added automatically so if you used createProject to add Kore, just remove it.

* 2017-04-02: What was previously Kore::Mixer and Kore::Audio is now Kore::Audio1 and Kore::Audio2.
* 2017-03-30: What was previously Kore::Graphics is now Kore::Graphics4.

# Battlefield: Bad Company — إعادة ترجمة ثابتة (Xbox 360) إلى Windows

مشروع **static recompilation** للعبة *Battlefield: Bad Company* (نسخة Xbox 360، Title ID `454107F9`)
إلى تطبيق Windows x64 أصلي، باستخدام **ReXGlue SDK 0.10.0**.

المستودع يحتوي **كود المشروع فقط** — لا يوجد فيه أي ملف من اللعبة الأصلية
(`game/` و `generated/` مستثنيان، والتفاصيل أدناه).

---

## المحتويات

| المسار | الوصف |
|---|---|
| `CMakeLists.txt` | هدف البناء `bfbc.exe` (WIN32) + إعداد إضافة الرسوميات وملف الخريطة |
| `bfbc_manifest.toml` | بيان إعادة الترجمة: نقطة الدخول، `setjmp/longjmp`، نقاط دخول دوال اكتُشفت يدوياً |
| `src/main.cpp` | تسجيل التطبيق (`REX_DEFINE_APP`) |
| `src/bfbc_app.h` | التطبيق: افتراضات التشغيل، تحميل إضافة الرسوميات، تسجيل إدخال الماوس/الكيبورد |
| `src/bfbc_graphics.h` | منيو F5: الدقة، الدقة الداخلية، VSync، الفلترة، حساسية الماوس، أزرار الكيبورد |
| `src/bfbc_patches.cpp` | تجاوز دوال `XeKeys*` في الـ SDK (تحقق توقيع القرص) |
| `build.ps1` | بناء كامل: configure ← codegen ← إصلاح الفروع ← Ninja |
| `build/fix_gen_branches.ps1` | إصلاح فروع يولّدها codegen كـ `REX_FATAL` رغم وجود الهدف |
| `build/add_funcs.ps1`, `build/analyze_image.ps1` | أدوات إضافة دوال مفقودة إلى البيان |
| `build/NOTES_missing_functions.md` | منهجية اكتشاف الدوال الناقصة وتفادي تقسيم الدوال |
| `build/run_bfbc.bat` | مشغّل مختصر (لم يعد ضرورياً) |

---

## المتطلبات

- Windows x64.
- **ReXGlue SDK 0.10.0** (`win-amd64`) — يوفر `rexglue.exe` و`rexruntime.dll` و`rexgpu-xenos.dll`.
- **LLVM/Clang 19.1.5** و **CMake 3.31.6** و **Ninja 1.13.2**.
- sysroot من `xwin` يحوي MSVC 14.44 و Windows SDK 10.0.26100 (لا حاجة لتثبيت Visual Studio).

المسارات مكتوبة في أعلى `build.ps1`، عدّلها لتطابق جهازك:

```powershell
$Toolchain = "D:\SAINTS ROW\build\toolchain"
$Sdk       = "D:\recomps\.sdk\rexglue-sdk-0.10.0\win-amd64"
```

---

## ملفات اللعبة (غير مرفقة)

ضع نسخة **تملكها قانونياً** داخل `game/` بحيث يوجد:

```
game/default.xex          # الصورة التنفيذية للعبة
game/Output/...           # بيانات المحتوى
game/dist/...
```

بدون `default.xex` لا يمكن توليد الكود ولا تشغيل اللعبة.

---

## البناء والتشغيل

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
build\bfbc.exe
```

`build.ps1` يقوم بالتسلسل التالي:

1. `cmake` configure بمترجم Clang وruntime ديناميكي.
2. `rexglue codegen bfbc_manifest.toml` — يولّد `generated/default/*` من `game/default.xex`.
3. `build/fix_gen_branches.ps1` — يصلح فروع codegen قبل تجميعها.
4. بناء Ninja ← `build/bfbc.exe`.

تشغيل `bfbc.exe` مباشرة (نقر مزدوج) يكفي: التطبيق يضبط لنفسه مسار `..\game`،
ويحمّل إضافة `xenos`، ويسمح للعبة بالكتابة في `GAME:\`، ثم ينشئ نظام الرسوميات.

---

## ما أُضيف فوق الـ SDK

1. **افتراضات التشغيل** (`OnConfigurePaths`): مسار بيانات اللعبة، `gpu_plugin=xenos`،
   و`allow_game_relative_writes` عند غياب أي تحديد من سطر الأوامر أو `bfbc.toml`.
2. **دقة الرندر تعمل فعلاً** (`OnPreSetup`): إضافة الرسوميات تسجّل متغيّراتها عند تحميل
   الـ DLL، والـ SDK يحمّلها بعد قراءة `bfbc.toml`، فكانت `draw_resolution_scale_x/y`
   تُهمل ويُبنى كاش أهداف الرندر على 1x دائماً. الآن يُحمَّل الـ DLL أولاً لتسجيل
   المتغيّرات، تُطبَّق القيم المحفوظة، ثم يُنشأ نظام الرسوميات.
3. **ماوس وكيبورد** (`OnPostSetup`): تسجيل `MnkInputDriver` الخاص بالـ SDK
   (WASD للحركة، الماوس للعصا اليمنى، وأزرار اليد على مفاتيح اللوحة).
4. **منيو F5** (`bfbc_graphics.h`): وضع العرض، دقة المخرج، الدقة الداخلية، VSync،
   الفلترة anisotropy، تجميع الشيدر، حساسية الماوس، وتعديل كل `keybind_*` —
   والكل يُحفظ في `bfbc.toml`.
5. **`bfbc_patches.cpp`**: `XeKeysConsolePrivateKeySign` و`XeKeysConsoleSignatureVerification`
   تعيد النجاح بدل 0، وإلا abort اللعبة على تحقق القرص.
6. **`fix_gen_branches.ps1`**: يعيد كتابة فروع شرطية يولّدها الـ emitter كـ `REX_FATAL`
   بينما هدفها موجود داخل نفس الدالة (`goto loc_XXXX`).

---

## القيود المعروفة

- **لا محدد فريمات**: SDK 0.10.0 لا يوفر `fps_limit`، و`video_mode_refresh_rate`
  مقيس أنه لا يؤثر على الإيقاع. المتاح هو VSync (معدل الشاشة) أو إيقافه.
- **تغيير الدقة الداخلية** يُحفظ في `bfbc.toml` ويُطبَّق بعد إعادة التشغيل، لأن
  كاش أهداف الرندر يُبنى مرة واحدة عند الإقلاع.
- **الماوس ليس raw**: اللعبة تقرأ عصا تماثلية فقط، فأي حل ماوس هو محاكاة للعصا
  اليمنى ويتبع منحنى اللعبة. الحساسية قابلة للضبط من F5.
- بعض الأصول/الإعدادات الاختيارية تظهر كتحذيرات `NtCreateFile` (ملفات غير موجودة
  في نسخة القرص) ولا تمنع التشغيل.

---

## ملاحظة قانونية

المستودع **لا يحتوي** أي ملف من اللعبة الأصلية ولا كوداً مولّداً منها.
`game/` و`generated/` مستثنيان في `.gitignore`. لتشغيل المشروع يجب أن تستخدم
نسخة من اللعبة تملكها.

---

## English summary

Static recompilation of *Battlefield: Bad Company* (Xbox 360) to a native Windows x64
executable using ReXGlue SDK 0.10.0. This repository contains only the project code:
the retail `game/` data and the generated `generated/` sources are excluded and must be
produced locally (`rexglue codegen`). Build with `build.ps1` after pointing it at your
ReXGlue SDK, LLVM and sysroot. Notable fixes: resolution scaling was silently ignored
because the GPU plugin registers its cvars after the config is read (fixed by preloading
the plugin and applying the values before the graphics system is created); the app now
starts without command-line arguments; keyboard/mouse controller emulation is registered;
and the F5 overlay exposes display, resolution, mouse sensitivity and key binding settings.
No game content is distributed here.

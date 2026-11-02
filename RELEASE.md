# إصدار الحزمة (Release) — v0.1.0

هذه الحزمة هي `bfbc.exe` مبنيّ بـ `Release` مع مكتبات التشغيل، معبّأ في مجلد
يستطيع المستخدم وضع ملفات لعبته فيه، ثم يُضغط في zip.

## ماذا تحتوي

```
BFBC-Recomp-v0.1.0-win64/
  bin/
    bfbc.exe            62 MB  - التطبيق المُعاد ترجمته (Release، x64)
    rexgpu-xenos.dll     2.7 MB - إضافة الرسوميات (Xenos، من الـ SDK)
    rexruntime.dll       9.9 MB - زمن تشغيل ReXGlue
    TracyClient.dll    231 KB  - أداة تتبع تُحمَّل من قبل الـ runtime
    bfbc.toml                 - إعدادات مبدئية (ملء الشاشة، مقياس دقة 2)
  game/                    - فارغ: المستخدم يضع نسخته (default.xex + المحتوى)
  licenses/SDL3-LICENSE.txt
  README.txt               - دليل تشغيل بالعربية + English quickstart
  README.md                - شرح المشروع والبناء من المصدر
  PUT-YOUR-GAME-FILES-HERE.txt
```

لا تُضمَّن أي ملفات من اللعبة، ولا الكود المولَّد (`generated/`)، ولا ملفات
التصحيح (`.map`, `.pdb`).

## إعادة إنتاج الحزمة

```powershell
# 1) بناء Release (يكتب build\bfbc.exe ومعه مكتبات التشغيل)
powershell -ExecutionPolicy Bypass -File build.ps1

# 2) تجميع المجلد
$rel = "release\BFBC-Recomp-v0.1.0-win64"
New-Item -ItemType Directory -Force "$rel\bin", "$rel\game", "$rel\licenses" | Out-Null
Copy-Item build\bfbc.exe, build\rexgpu-xenos.dll, build\rexruntime.dll,
           build\TracyClient.dll, build\bfbc.toml "$rel\bin"
Copy-Item README.md "$rel"
Copy-Item (Join-Path $env:REXSDK "licenses\SDL3\LICENSE.txt") "$rel\licenses\SDL3-LICENSE.txt"

# 3) الضغط
Compress-Archive -Path $rel -DestinationPath "release\BFBC-Recomp-v0.1.0-win64.zip" -Force
```

`release/` مستثنى في `.gitignore`: الحزمة تُنشر كملف مرفق (asset) في صفحة
الإصدارات، لا تُرفع داخل المستودع.

## اختبار الحزمة (تم قبل النشر)

شُغّلت الحزمة من داخل تخطيطها النهائي (`bin\bfbc.exe` مع `game\` بجانبها):

- النافذة ظهرت وبقيت مستجيبة: `bfbc [rexglue-v0.10.0-Release]`.
- `draw_resolution_scale_x/y = 2` و`resolution_scale = 2` طُبِّقت قبل تهيئة الرسوميات.
- `Input: keyboard/mouse driver registered`.
- بصمة `bin\bfbc.exe` مطابقة لمخرَج البناء.

## رفع الإصدار على GitHub

```powershell
gh release create v0.1.0 "release\BFBC-Recomp-v0.1.0-win64.zip" `
  --title "BFBC Recomp v0.1.0 (Windows x64)" `
  --notes-file release\NOTES-v0.1.0.md
```

## تنبيه

`bfbc.exe` مبنيّ من كود أُعيد توليده من `default.xex`، أي أنه يحتوي كوداً
مشتقاً من اللعبة. نشره علناً يعني توزيع كود اللعبة؛ البديل الآمن هو نشر
المصدر فقط (`build.ps1` + `bfbc_manifest.toml`) وترك كل مستخدم يبني نسخته
من ملفاته.

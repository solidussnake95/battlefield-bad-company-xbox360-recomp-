================================================================
ملخص كامل للعمل — BFBC ReXGlue Recomp
التاريخ: 2026-11-03
================================================================

مسار المشروع (المجلد الرئيسي):
  D:\recomps\bfbc-recomp

برنامج التشغيل (البناء الحالي):
  D:\recomps\bfbc-recomp\build\bfbc.exe

ملف الإعدادات:
  D:\recomps\bfbc-recomp\build\bfbc.toml

حزمة الإصدار:
  D:\recomps\bfbc-recomp\release\BFBC-Recomp-v0.1.0-win64\bin\bfbc.exe

ملفات المصدر:
  D:\recomps\bfbc-recomp\src\bfbc_app.h
  D:\recomps\bfbc-recomp\src\bfbc_graphics.h
  D:\recomps\bfbc-recomp\src\bfbc_mouse.h
  D:\recomps\bfbc-recomp\src\bfbc_mouse.cpp

أداة البناء:
  D:\recomps\bfbc-recomp\build.ps1

ملفات SDK:
  D:\recomps\.sdk\rexglue-sdk-0.10.0\win-amd64

ملاحظة:
  C:\Users\Omar\bfbc-recomp = نسخة قديمة منفصلة (غير مستخدمة).

----------------------------------------------------------------
1) إصلاح مؤشر الماوس في قائمة F5  (مكتمل ومُختبر)
----------------------------------------------------------------
المشكلة:
  عند فتح قائمة F5 الخاصة بالرسومات (Graphics) كان المؤشر يختفي
  ولا يمكن النقر على القائمة.

السبب:
  - نظام mnk_mouse (الماوس كلعبة) يقوم بحجز المؤشر (pointer capture)
    وإخفائه داخل نافذة اللعبة.
  - الـ SDK يقوم بفك الحجز فقط لواجهاته الرسمية (F3 / F4 / Console)،
    لكنه لا يعرف شيئاً عن قائمة F5 المخصصة التي أضفناها.
  - النتيجة: المؤشر يبقى محجوزاً ومخفياً خلف نافذة اللعبة.

الحل المُنفَّذ (في src/bfbc_app.h):
  - عند فتح القائمة: استدعاء
        rex::input::mnk::SetMouseLookActive(false)
    => يحرر المؤشر ويُظهره.
  - عند إغلاق القائمة أو الإغلاق الكامل: استدعاء
        SetMouseLookActive(true)
    => يعيد الحجز للعبة بشكل صحيح.
  - أُضيفت دالة مساعدة CloseGraphicsDialog() لتجميع منطق الإغلاق
    في مكان واحد، مع تأجيل الإغلاق داخل CallInUIThread حتى لا
    تُدمَّر القائمة أثناء رسمها.

الاختبار:
  - اختُبر آلياً عبر PowerShell باستخدام GetCursorInfo، والنتائج:
      * قبل فتح F5: المؤشر ظاهر = True.
      * بعد فتح F5: المؤشر ظاهر = True   [مُصلَح، كان يختفي]
      * بعد إغلاق F5: المؤشر مخفي/محجوز = False   [الحجز عاد للعبة]
  - أُعيد البناء بنجاح: D:\recomps\bfbc-recomp\build\bfbc.exe

----------------------------------------------------------------
2) خلل غير مُصلَح: مشغّل الماوس المخصص MouseShapeDriver ميت فعلياً
----------------------------------------------------------------
  - أُنشئت ملفات جديدة: src/bfbc_mouse.h و src/bfbc_mouse.cpp
    وهي طبقة تُعيد تشكيل عصا التحكم اليمنى (gain / curve / deadzone)
    بعد مشغّل الـ SDK.
  - لكن: نسخة الـ SDK 0.10.0 تسجّل MnkInputDriver افتراضياً،
    ومشغّلنا يستخدم نفس Device ID، فيفوز مشغّل الـ SDK دائماً.
  - النتيجة: متغيّرات bfbc_mouse_* لا تُطبَّق أبداً، ولا تظهر أسطر
    MouseShape في السجلات.
  - الحلول المقترحة (لم تُطبَّق):
      * إزالة تسجيل MnkInputDriver الافتراضي من الـ SDK، أو
      * تغيير Device ID / طريقة التسجيل.

----------------------------------------------------------------
3) مشكلة الإضاءة عالية الدقة  (قيد التحقيق — لم يُطبَّق أي إصلاح)
----------------------------------------------------------------
وصف المشكلة من المستخدم:
  عند رفع دقة العرض الداخلية (Internal Resolution / supersampling)
  إلى 2x أو 3x، تتغير الإضاءة / الإضاءة الشاملة (GI) بشكل غير طبيعي
  مع تغيّر البيئة. أرفق المستخدم فيديو 1920x1080 بطول 5.28 ثانية.

ما تم عمله:
  - تأكيد خصائص الفيديو عبر ffprobe
    (1920x1080، 5.28s، ~59.6 fps).
  - استخراج 11 إطاراً بمعدل 2 إطار/ثانية إلى:
      C:\Users\Omar\AppData\Local\Temp\opencode\frames\f_01.jpg ... f_11.jpg
  - استعراض الإطارات: المشهد يبدو مُعتماً بشكل واضح
    (غابة/منزل في مشهد ليلي أو ظلّي).
  - ملاحظة: في لقطة الشاشة تظهر نسخة من صورة قائمة F5 الجديدة
    معروضة جانب نافذة اللعبة.

الفرضيات المطروحة (مستندة إلى إصلاحات مجتمع Xenia المعروفة
للعبة Bad Company):
  1. draw_resolution_scaled_texture_offsets = false
     => الإصلاح الأشهر لمشاكل السطوع/الإضاءة في Bad Company
        عند رفع الدقة.
  2. half_pixel_offset
  3. resolve_resolution_scale_fill_half_pixel_offset
  4. readback_resolve
     => يحسّن السطوع لكنه يسبب آثاراً جانبية وأداءً أسوأ.

ملاحظة: لم يُغيَّر أي كود بعد بخصوص الإضاءة، ولم تُضَف أي خيارات
لها في قائمة F5.

----------------------------------------------------------------
4) حالة المستودع والبناء
----------------------------------------------------------------
المسار الفعلي للتطوير:
  D:\recomps\bfbc-recomp
  (يوجد نسخة قديمة منفصلة في C:\Users\Omar\bfbc-recomp — غير مستخدمة)

الملفات المعدّلة (غير مُودَعة في git):
  - CMakeLists.txt                                  (معدّل)
  - src/bfbc_app.h                                  (معدّل — إصلاح المؤشر)
  - src/bfbc_graphics.h                             (معدّل — قائمة F5)
  - src/bfbc_mouse.cpp / src/bfbc_mouse.h           (جديدة وغير متعقَّبة)

البناء الحالي:
  D:\recomps\bfbc-recomp\build\bfbc.exe
  2026-11-03 21:22  —  62,558,208 بايت  —  ناجح

ملف الإعدادات الحالي build\bfbc.toml:
  fullscreen = false
  draw_resolution_scale_x = 1
  draw_resolution_scale_y = 1
  resolution_scale = 1
  vsync = false
  bfbc_mouse_gain = 2.0
  bfbc_mouse_curve = 1.0
  bfbc_mouse_deadzone = 0.0
  bfbc_mouse_debug = true
  resolution = ""

حزمة الإصدار (قديمة):
  D:\recomps\bfbc-recomp\release\BFBC-Recomp-v0.1.0-win64\bin\bfbc.exe
  بتاريخ 2026-11-02 12:35
  (لم تُحدَّث بإصلاح المؤشر بعد)

ملاحظة: إجراء التغليف موثّق في RELEASE.md
(نسخ build\bfbc.exe + ملفات DLL + bfbc.toml إلى مجلد release\...\bin
ثم إنشاء ملف zip).

----------------------------------------------------------------
5) الخطوات التالية المقررة (عند الاستئناف)
----------------------------------------------------------------
  1. تشخيص مشكلة الإضاءة بدقة أكبر (مقارنة لقطات عند 1x و2x و3x).
  2. إضافة قسم "Lighting / Compatibility" في قائمة F5 يتضمن:
       - draw_resolution_scaled_texture_offsets
       - half_pixel_offset
       - resolve_resolution_scale_fill_half_pixel_offset
       - (اختياري) readback_resolve
  3. حفظ هذه القيم في bfbc.toml والتحقق من أنها تُطبَّق قبل تهيئة
     GPU (عبر OnPreSetup).
  4. إعادة البناء والاختبار على 2x و3x.
  5. تحديث حزمة الإصدار بنسخ build\bfbc.exe + مكتبات DLL + bfbc.toml
     إلى مجلد release\...\bin.
  6. (اختياري) إصلاح مشغّل MouseShapeDriver الميت.

================================================================
تم التوقف هنا بناءً على طلب المستخدم.
================================================================

================================================================
6) تحديث — 2026-11-03 (بعد الاستئناف): تم إنجاز البنود 2 و3 و6
================================================================

أ) إصلاح مشغّل MouseShapeDriver  (مكتمل ومُتحقَّق)
  السبب الحقيقي (مصدر SDK):
    CreateDefaultInputSystem() يسجّل MnkInputDriver دائمًا. كل نسخة منه
    تُعلن نفس معرّف الجهاز الثابت 0x4D4E4B00، و
    InputSystem::DriverForDevice() يُرجع أول مشغّل أبلغ عن هذا المعرّف،
    فمشغّلنا كان مُسجَّلاً لكن لا يُستدعى أبدًا.
  الحل:
    إنشاء نظام إدخال خاص عبر RuntimeConfig::input_factory
    (المتوثّق الرسمي) في OnPreSetup:
      - SDL أو XInput حسب input_backend
      - MouseShapeDriver بدل MnkInputDriver الافتراضي
      - NopInputDriver + SlotAssignment كما في الـ SDK
    الملفان: src/bfbc_mouse.h / src/bfbc_mouse.cpp
  التحقق من السجل:
    Input: custom input system built (pads=sdl, keyboard/mouse=shaped)
    MouseShape: right-stick shaping is live
    raw=(200,0) shaped=(400,0) gain=2.00   (والقص عند 32767)

ب) قسم "Lighting / Compatibility" في قائمة F5  (مكتمل ومُتحقَّق)
  ملاحظة مهمة: الأسماء الواردة في البند 3 القديم غير موجودة في
  rexgpu-xenos 0.10.0 (لا يوجد half_pixel_offset ولا readback_resolve).
  الأسماء الفعلية المضافة:
    draw_resolution_scaled_texture_offsets         (bool)
    resolve_resolution_scale_fill_half_pixel_offset (bool)
    readback_resolve_half_pixel_offset             (bool)
    d3d12_readback_resolve                         (none/fast/some/full)
  - تُحفظ في bfbc.toml وتُدفع قبل تهيئة GPU (kStartupOnlyFlags في OnPreSetup).
  - تحقّق السجل: كل الأربعة طُبّقت "before GPU init" بدون رفض.
  - أُصلح خلل quoting كان يمنع قراءة d3d12_readback_resolve من السجل
    (المكوّن يُرجع القيمة بصيغة الملف quotée).
  - ما زال الحكم على الإصلاح البصري عند 2x/3x يحتاج تجربة داخل اللعبة.

ج) إطلاق السلاح بالماوس  (مكتمل ومُتحقَّق) — طلب المستخدم
  - SDK يسجّل أزرار الماوس كمفاتيح افتراضية: LMB / RMB / MMB.
  - الإعداد الافتراضي الجديد (يُطبَّق فقط إذا لم يكن في bfbc.toml):
      keybind_right_trigger = "LMB,E,O"   (إطلاق)
      keybind_left_trigger  = "RMB,Q,I"   (تصويب)
  - تحقّق من السجل بالنقر آليًا على نافذة اللعبة:
      lt=0  rt=255   عند ضغط الزر الأيسر
      lt=255 rt=0    عند ضغط الزر الأيمن

د) أيقونات الأزرار داخل اللعبة حسب نوع الإدخال  (غير ممكن بالبيانات)
  - اللعبة لا تستدعي أي API كيبورد (لا XamInputGetKeystroke)،
    و XamInputGetCapabilities تُستخدم فقط لكشف المقود.
  - كل رسومات الـ UI (ومنها أيقونات الأزرار) داخل حزم Frostbite:
      dist/xenon/menu/main/mainmenu-00.fbrb
      dist/xenon/menu/hud/hud_*.fbrb
    ولا توجد مجموعة أيقونات كيبورد/ماوس في قرص نسخة الـ 360.
  - البديل الواقعي: استبدال النواميس (textures) داخل هذه الحزم،
    أو إظهار تلميحات من طبقتنا (ImGui) بدل أيقونات اللعبة.

هـ) حالة البناء والإصدار
  build\bfbc.exe  2026-11-03 22:37  sha256 9de2d4a63b1ae40e...
  حزمة الإصدار: تحديث bin + README + إعادة ضغط الـ zip.
  الملفات المعدّلة: CMakeLists.txt, src/bfbc_app.h, src/bfbc_graphics.h,
                   src/bfbc_mouse.cpp (جديد), src/bfbc_mouse.h (جديد),
                   README.md, RELEASE.md

و) سلاسة الماوس (rate-based aiming)  — مُنفَّذ ومُتحقَّق بالأرقام
  السبب:
    اللعبة تحوّل انحراف العصا إلى "سرعة دوران"، وتتجاهل أي انحراف داخل
    الـ deadzone الخاص بها (7849 من 32767، وهو حد XInput الذي يعلنه
    الـ runtime). وطبقتنا كانت ترسل نسبة من حركة الماوس بالفريم مباشرة،
    فحركة بطيئة (1-5 بكسل) تعطي 200-1000 => صفر حركة، والتحريك السريع
    يقفز فوق الحد. النتيجة: "مو سلس أبداً".
  الحل في bfbc_mouse.h:
    - bfbc_mouse_floor (default 7849): أصغر انحراف يُرسل عند أي حركة،
      فتخرج أي حركة من deadzone اللعبة. (0 = تعطيل)
    - bfbc_mouse_time_normalize (default true): قسمة الحركة على فاصل
      الاستدعاء المقاس، فسرعة الدوران تعتمد على سرعة اليد لا على الفريمات
      (60/120/144 fps تعطي نفس الإحساس). مُقيَّد بين 1/240 و1/20 ثانية.
    - الدالة ShapeAxis صارت خريطة رتيبة: t = min(1, |raw|*gain) ثم curve
      ثم out = floor + (1-floor)*t
  التحقق من السجل (بناء 22:52):
    raw=(0,-200)  =>  shaped=(0,-7900)     [كان 200 = صفر حركة]
    الحركة الآن رتيبة: 1px ≈ 7900، 20px ≈ 10900، 41px ≈ 14000،
    120px ≈ 26000، وأكثر => تشبّع كامل.
  التظبيط من F5 (قسم Mouse):
    gain = حساسية، curve = دقة القريب، deadzone = تجاهل الاهتزاز،
    floor = تعويض deadzone اللعبة، و Frame-rate independent aim.
  القيد المتبقي:
    الإحساس 1:1 الحقيقي (بدون تسريع/تنعيم اللعبة) يحتاج تعديل كود
    النظر داخل اللعبة نفسها، لأن العصا تمر عبر منحنى اللعبة.

ز) محاولة "Patch حقيقي للنظر" + بريسيت PC aim
  ما فُحص:
    - اللعبة تقرأ الباد من نقطة واحدة: sub_8291D6D8 -> XamInputGetState
      (bfbc_recomp.93.cpp:16601)، تُدعى مرة كل فريم من وحدة الإدخال.
    - لا يوجد أي مسار ماوس/كيبورد في المرسل، ولا symbols في 192MB كود
      مولّد، والكاميرا تستهلك العصا داخل منحنى محرك Frostbite.
    - لذلك تعديل دالة دوران الكاميرا (لتجاوز المنحنى) غير عملي بأمان
      بدون RE عميق؛ الخلاصة: أقرب إحساس PC يتم بتعويض منحنى اللعبة.
  ما نُفّذ:
    - زر "Preset: PC aim" في F5 -> Mouse: gain 2.00, curve 0.75,
      deadzone 0, floor 7849, frame-rate independent (live).
    - curve < 1 يعوّض ضعف بداية الحركة في منحنى اللعبة (تسريعها).
  ملاحظة بناء: build\bfbc.exe كان مفتوحًا (جلسة المستخدم) فتم الربط
  إلى bfbc_new.exe ونُسخ إلى release\...\bin\bfbc.exe ثم حُذف.
  إعادة بناء build\bfbc.exe تحتاج إغلاق اللعبة أولًا.

ح) تغيير إعدادات الكيبورد + إصلاح تحميل الـ keybinds
  الخريطة المطلوبة (A=F, B=C, Ctrl, X=R, RT=LMB, LT=RMB, RB=2, LB=Q, PAD=الأسهم):
    keybind_a = F
    keybind_b = C,Ctrl
    keybind_x = R
    keybind_left_shoulder = Q
    keybind_right_shoulder = 2
    keybind_right_trigger = LMB
    keybind_left_trigger = RMB
    keybind_dpad_up/down/left/right = Up/Down/Left/Right
  خلل مُكتشف ومُصلح:
    keybind_* تُسجَّل من وحدة الإدخال بعد قراءة bfbc.toml، فالقيم المحفوظة
    في الملف لم تكن تصل للـ cvar أبدًا (كل تشغيل يستخدم افتراضيات SDK:
    a=Semicolon,Space, x=L ...). أُضيف ApplySavedKeyBinds() في
    OnConfigurePaths يدفع كل keybind_* من الملف قبل الافتراضيات،
    وApplyDefaultBind صار يفرّق بين "مفتاح غير موجود" و"مفتاح فارغ".
  تحقق من السجل: fire='LMB' aim='RMB' | a='F' b='C,Ctrl' x='R' lb='Q' rb='2'
                  dpad='Up','Down','Left','Right'

ط) تشخيص الظلال (مفتوحة — تحتاج تجربة المستخدم)
  العَرَض: الظلال تختفي أحيانًا وغير متناسقة مع البيئة (مثل الأشجار)،
           على كل الدقات (1x/2x/3x) => ليست مشكلة resolution scaling.
  القيم الفعلية من البلجن (rexgpu-xenos 0.10.0):
    depth_float24_convert_in_pixel_shader = false   <-- مشتبه أول
    depth_float24_round                   = false
    depth_transfer_not_equal_test         = true
    gpu_3d_to_2d_texture                  = true
    texture_cache_memory_limit_render_to_texture = 24 MB (soft 384 / hard 768)
  سبب الاشتباه: خرائط الظل تُرسم في أسطح عمق ثم تُقرأ كنسيج؛ convert_in_ps
    يتحكم بتحويل عمق float24 عند القراءة، وبقاء خرائط الظل في كاش
    الـ render-target (24MB) يعني إخلاءها تحت الضغط => ظل يختفي/يتأخر.
  ما أُضيف: قسم "Shadows / depth" في F5 (4 مفاتيح + حجم كاش RT)،
    تُحفظ في bfbc.toml وتُدفع قبل تهيئة GPU (kStartupOnlyFlags)،
    وسطر تشخيص "GPU: rt limit=..." عند الإقلاع.
  ترتيب التجربة الموصى به: convert_in_ps=ON ثم round=ON ثم RT cache=128
    ثم not_equal=OFF ثم 3d2d=OFF (واحد واحد مع مراقبة ظل شجرة).

ي) تحديد سبب "قطع/اختفاء ظل الشجرة مع المسافة" (2026-11-04)
  العَرَض المؤكَّد من المستخدم: ظل الشجرة يظهر قريبًا ويُقطع/يختفي مع المسافة
  => هذه إعدادات خرائط الظل داخل بيانات فروستبايت، وليست دقة العرض.

  الأدوات الجاهزة على النت (للمرجع فقط، لا حاجة لها):
    - BFBC2 Toolkit / FBOneTools (HeicoDev) — فتح/تعديل .fbrb و.dbx.
    - سكربتات Frankelstner الأصلية: fbrb.pyw و dbx.pyw (داخل مستودع الأداة).

  ما أُضيف محليًا (بايثون 3، بدون اعتماديات):
    - tools/fbrb.py          فك/قائمة/بحث/إعادة بناء أرشيف .fbrb
                             (FbRB + gzip(part1) + gzip(payload)).
    - tools/dbx.py           تحويل .dbx إلى XML (قراءة قيم الإعدادات).
    - tools/config_patch.py  تعديل كتلة "bfbc port overrides" في Game.cfg
                             داخل config.zz مع نسخة احتياطية/استرجاع.

  القيم الافتراضية المستخرجة من scenes/uibackground.dbx (نوع RenderSettings):
    ShadowmapViewDistance = 100.0        ShadowmapSliceCount = 1
    ShadowmapMinScreenArea = 0.05        ShadowmapSliceSchemeWeight = 0.8
    ShadowmapSliceOverlapFactor = 0.0025 ShadowmapExtrusionLength = 30.0
    ShadowmapWidth/Height = 1024         ShadowmapFirstSliceScale = 1.0
    CascadedBoxShadowmapEnable = false   ShadowmapFixedMovementEnable = true
  أي أن الظل يُرسم فقط داخل 100 متر، مع cull لأي جسم مساحته على الشاشة
  أقل من 5% => شجرة بعيدة يختفي ظلها قبل الشجرة نفسها.

  التعديل المطبَّق: بروفايل shadow-far في game\dist\xenon\config.zz:
    WorldRender.ShadowmapsEnable true
    WorldRender.ShadowmapViewDistance 200
    WorldRender.ShadowmapMinScreenArea 0.005
  (أُزيل Render.ForceFov 30 الذي كان تجربة سابقة، وبقي كتعليق.)
  نسخ احتياطية:
    config.zz.bfbc-orig   (الأصل من القرص)
    config.zz.bfbc-prev   (قبل هذا التعديل)
    config.zz.bfbc-backup (تلقائي من أداة التعديل)

  الاسترجاع:
    python tools\config_patch.py restore game\dist\xenon\config.zz
  التجربة التالية إذا صار الظل أبعد لكنه خشن:
    python tools\config_patch.py apply game\dist\xenon\config.zz shadow-cascades
  ملاحظة: لم يُتحقَّق بعد أن المحرك يقرأ config.zz في هذا الـ recomp؛
  الدليل العملي = هل تغيّر مدى الظل داخل اللعبة.

ي-1) تحليل كراش أول تجربة لـ shadow-far (2026-11-04)
  bfbc_046.log: إقلاع 12:41:41، آخر سطر 12:41:51 (قراءة قائمة F5)،
  ثم لا شيء. WER: APPCRASH، الموديول rexruntime.dll، 0xc0000005،
  offset 0x253a88.

  فُكّ العنوان يدويًا (لا يوجد rexruntime.pdb؛ استُخرج من جدول التصدير):
    0x253a80  XEvent::Set  =>  sub rsp,0x28 / mov rcx,[rcx+0x68]
    0x253a88  <-- crash:     mov rax,[rcx]
    0x253a8b                 call [rax+0x10]
  أي أن XEvent (كائن حدث نواة/خيوط) استُخدم بعد تحريره أو بمؤشر صفري،
  ثم استُدعيت دالته الافتراضية. هذا عطل في rexruntime نفسه (18 موقع نداء
  لـ XEvent::Set داخل الـ DLL)، وليس في بيانات الظل.

  سجل الكراشات السابق (Application Error, آخر 3 أيام) يثبت أن المشروع
  غير مستقر أصلًا: ntdll 0xc0000374 (فساد كومة) 2026-11-02/03،
  ucrtbase 0xc0000409 (abort) عدة مرات، rexruntime 0x80000003
  (int3/assert) على 0x9fe30 عشرات المرات، وbfbc.exe 0xc0000005
  على عناوين مختلفة. إذًا 0x253a88 عنوان جديد لكن الظاهرة قديمة.

  للفصل بين السببين: أُعيد config.zz إلى نسخة القرص الأصلية حرفيًا
  (SHA256 56A8C488A534F783... = config.zz.bfbc-orig)، وهكذا أُزيل أيضًا
  Render.ForceFov 30 المتبقي من تجربة سابقة.
  أُضيف بروفايل "none" في tools/config_patch.py لحذف كتلة التجاوزات.
  التجربة التالية = تشغيل تحكّمي بدون أي تعديل، ثم إعادة shadow-far.

ي-2) نتيجة تجربة shadow-far (فيديو 2026.11.04-12.49.mp4)
  المقطع 9.7 ثانية، 1080p60، مستوى sp_01_greenacres ليلًا.
  استُخرجت إطارات بـ ffmpeg وحُلِّلت:
  - الظل صار يمتد أبعد فعلًا (تأكيد أن المحرك يقرأ config.zz ويقبل
    أسماء WorldRender.*) — أي المسار صحيح.
  - بقيت المشكلة الأساسية: حدّ ظل مستقيم/مسنّن يقطع الأرض، وهو حافة
    خريطة الظل الواحدة (200 م) مع كثافة تكسيل منخفضة عند نهايتها.
  => الحل الصحيح ليس زيادة المدى أكثر، بل عدة شرائح (cascades) لكل منها
     دقتها، بدل خريطة واحدة موزّعة على مسافة كبيرة.

  بروفايلات config_patch.py الآن:
    none            -> Game.cfg كما شُحن
    shadow-far      -> 200 م، خريطة واحدة
    shadow-wide     -> 300 م + 2048px + extrusion 100 (بدون cascades)
    shadow-cascades -> 3 شرائح حتى 350 م + 2048px + extrusion 100
  المطبَّق حاليًا: shadow-cascades.
  ملاحظة: apply صار يستطيع إضافة الكتلة إن لم تكن موجودة في Game.cfg الأصلي.

ي-3) النتيجة: cascades = شاشة سوداء
  صورة المستخدم: العالم كله أسود، الـHUD سليم (F5 يعمل).
  التفسير: تقنيات الرسم في نسخة Xenon مُصرَّفة على ShadowmapSliceCount=1،
  فتفعيل CascadedBoxShadowmap يجعل كل بكسل يقرأ خارج الخريطة => يُعتبر
  في الظل => إضاءة مباشرة صفر => سواد كامل. ليست مسألة قيم، بل مسار shader
  غير موجود في هذه النسخة.
  => الرجوع إلى خريطة واحدة، لكن بدقة أعلى ومدى أطول:
     shadow-wide: 300 م + 2048px + MinScreenArea 0.002 + extrusion 100

ي-4) تأكيد التشخيص + الحل النهائي (فيديو 13.00)
  shadow-wide (300 م): ظهر شريط ظل بحدٍّ مستقيم تمامًا يقطع الوادي (لقطة 12.56).
  shadow-diag (1000 م، 2048px، extrusion 30): الشريط المستقيم اختفى تمامًا،
  وبقيت ظلال التلال بحدود منحنية تتبع التضاريس => الخط كان حافة خريطة الظل
  (خارج الخريطة يُحسب في الظل)، وليس ظلًّا حقيقيًا. وأيضًا extrusion 100
  لم يكن سببًا.
  => الحل: مدى أطول من مدى الرؤية (الضباب يخفي الحافة) + دقة أعلى لتعويض
     التوزيع: shadow-final = 700 م + 4096px + extrusion 30 + MinScreenArea 0.002.
     المطبَّق حاليًا: shadow-final (لم يُجرَّب بعد).
  لو 4096 غير مدعوم (تشوّه/كراش) => الرجوع إلى 2048 مع 700–1000 م:
     python tools\config_patch.py apply game\dist\xenon\config.zz shadow-diag

ي-5) 4096 مرفوض + مقارنة مع الجهاز الأصلي (2026-11-04 13:04)
  shadow-final (4096px @ 700 م) = إطار رمادي مسطّح بلا عالم (لقطة 13.04):
  الظلال/العالم اختفت. => 4096 غير قابل للاستخدام في مسار Xenon.
  القياس (متوسط سطوع Y): 13.04 = 35.9 ، 12.56 (shadow-wide) = 32.6 => المشكلة
  ليست سطوعًا عامًّا بل غياب المشهد نفسه.

  المقارنة مع لقطة الجهاز الأصلي (نهار مشمس، ظلال أشجار منقّطة على الأرض):
  الفرق الأساسي عندنا = الأرض البعيدة/معظم المشهد يُحسب «في الظل» لأن ما خارج
  صندوق خريطة الظل يعامله المحرك ظلًّا => كل ما هو أبعد من المدى يصبح مضاءً
  بالـambient فقط => منظر ليلي/قاتم، بينما الأصل فيه شمس على كل المدى.
  أي أن القيود: خريطة واحدة بلا cascades (cascades = سواد كامل) + سلوك
  «خارج الخريطة = ظل».

  الخيارات المطروحة (كلها 2048px، extrusion 30):
    shadow-final : 1000 م — أفضل حالة مجرَّبة (لا حافة، ظل قريب مقبول).
    shadow-long  : 2500 م — يقترب من شكل الأصل (تلال بعيدة مضاءة) لكن تفقد
                   حِدّة ظل الأشجار القريب لأن التكسيل يصبح ~1.2 م/بكسل.
  المطبَّق حاليًا: shadow-long (2500 م) — اختيار المستخدم للمقارنة مع الأصل.

ي-6) محاولة التشخيص البصري (shadow-debug، فيديو 13.15)
  فُعِّلت: DrawShadowFrustums / DrawDebugShadowmaps / DrawLightSources.
  النتيجة: لا ظهر أي صندوق ولا خريطة ظل على الشاشة => هذه الأعلام غير منفَّذة
  (أو خلف build flag) في نسخة Xenon، فلا يمكن رؤية حدود الخريطة مباشرة.
  البديل: قياس غير مباشر — موضع الحد يتغيّر مع ViewDistance (100→300→1000)
  وهذا كافٍ لاعتبار العيب «صندوق خريطة واحدة»، مع احتمال مساهمة extrusion 100
  في تجربة 300 م (لم تُعزل لأنها تغيّرت مع المسافة في نفس التجربة).

ي-7) سبب الكراش الحقيقي: 2048+ في ShadowmapWidth (2026-11-04 13:19)
  سجل bfbc_054.log قبل الكراش مباشرة:
    [error] [gpu] Resolve region 0 <= x < 2048 is outside the surface pitch 1040
  أي أن مسار الـ resolve يقرأ عرض 2048 بينما السطح مُخصَّص بعرض 1024
  (pitch 1040 = 1024 + هامش). => إعداد ShadowmapWidth/Height فوق 1024
  يخرج عن حدود السطح => قراءة/كتابة خارج المدى => فساد ثم كراش في
  XEvent::Set (0x253a88) خلال 0.1 ثانية من آخر خطأ.

  الإحصاء (عدد أخطاء pitch في كل جلسة):
    045 (03:05، بلا تجاوزات)        0
    046 (12:41، shadow-far 1024)    0   <= كراش هذه الجلسة سببه Runtime سابق
    047 (12:49، shadow-far 1024)    0   <= "أحسن شوية" الذي رآه المستخدم
    048 (12:53، shadow-wide 2048)   1821
    049 (12:56، shadow-wide 2048)   6492
    050 (13:00، shadow-diag 2048)   4995
    051 (13:04، shadow-final 4096)  3438
    053 (13:15، shadow-debug 2048)  7248
    054 (13:19، shadow-long 2048)   6   <= ثم كراش
  => كل حالات السواد/الإطار المسطّح/الكراش بعد إدخال 2048 سببها هذا الخطأ.
  => القاعدة: ShadowmapWidth/Height يبقيان 1024؛ المتغيّران الآمنان هما
     ShadowmapViewDistance (صندوق الإسقاط) و ShadowmapMinScreenArea.
  بروفايل جديد shadow-safe = 1024px @ 300 م + MinScreenArea 0.005 (مطبَّق).

ي-8) الحالة النهائية المعتمدة (2026-11-04 13:23)
  shadow-safe = ShadowmapsEnable true / ViewDistance 300 / MinScreenArea 0.005
  مع إبقاء ShadowmapWidth/Height على 1024 (بدون أي تعديل).
  الجلسة bfbc_055.log: 0 أخطاء pitch، 0 كراش، إغلاق نظيف
  ("Window closing, shutting down..." ثم "Title terminated").
  المستخدم أكّد أن النتيجة صحيحة.
  الاسترجاع للأصل: python tools\config_patch.py apply ... none
  أو نسخ config.zz.bfbc-orig فوق config.zz.

----------------------------------------------------------------
ي-9) خيار FOV + محدد الفريمات (2026-11-04)
----------------------------------------------------------------
1) محدد الفريمات (مطبَّق + بُني بنجاح)
  - SDK 0.10.0 لا يوفّر أي cvar لتحديد الإطارات، والـPresenter ينظّم إطارات
    واجهة ImGui فقط. النقطة الوحيدة المتاحة لكل إطار على خيط الضيف هي
    استيراد VdSwap الذي يناديه المحرك مرة واحدة لكل إطار معروض.
  - ملفان جديدان: src/bfbc_frame.h و src/bfbc_frame.cpp
      * إعادة تعريف __imp__VdSwap داخل التطبيق (نفس أسلوب bfbc_patches.cpp)
        ثم مناداة النسخة الحقيقية عبر GetProcAddress من rexruntime.dll.
      * بعد الـswap: PaceFrame ينام ما تبقّى من الإطار حسب bfbc_fps_limit
        (0 = بلا حد)، ويعيد المزامنة إذا تأخّر الإطار أكثر من فترتين.
      * cvar: bfbc_fps_limit (0..480) يُقرأ كل إطار => تطبيق فوري بلا إعادة تشغيل.
  - F5: خيار "Frame rate limit" في قسم Rendering، مع عرض الزمن بالمللي ثانية،
    ويُحفظ في bfbc.toml مع بقية إعدادات القائمة (LoadFromCVars/PendingEntries).
  - البناء: D:ecompsfbc-recompuildfbc.exe — نجح بلا أخطاء.

2) خيار FOV (إعداد config، يحتاج إعادة تشغيل)
  - البحث: لا يوجد مفتاح FOV في Game.cfg المشحون ولا في سكربتات Lua داخل
    config.zz، و FieldOfView الظاهر في البيانات يخص مشاهد القوائم/الغان رَك
    (Scene.SceneSettings و ZoomLevelData) وليس كاميرا اللعب، ولا يوجد له أصل
    (asset) منفصل لكل مستوى.
  - من build/image_dump.bin (صورة الـXEX المفكوكة) ظهر جدول أسماء إعدادات
    المحرك وفيه ForceFov بجانب PerfOverlayEnable/PerfOverlayVisible، وهي من
    نفس الصنف الذي يقرأه اللعب من Render.PerfOverlayEnable => المفتاح الصحيح
    هو Render.ForceFov داخل كتلة applySettings.
  - tools/config_patch.py: أمر جديد
        python tools\config_patch.py fov <config.zz> <درجات|off>
    يكتب/يحدّث/يحذف سطر Render.ForceFov داخل كتلة التجاوزات، ويحفظه عند
    تغيير البروفايل (apply لا يمسحه). المدى المسموح 45..120 درجة عمودية.
  - طُبِّق Render.ForceFov 85 على game/dist/xenon/config.zz، ثم أعاد المستخدم
    التشغيل وأكّد أن المنظر أوسع (لقطتا شاشة من داخل اللعبة).

  اختبار الأداة (على نسخة build/fovtest.zz ثم حُذفت):
    fov 78 => ظهر السطر داخل applySettings
    apply shadow-safe => أبقى سطر FOV مع إعدادات الظل
    fov off => حُذف السطر وبقيت إعدادات الظل

3) سلايدر FOV داخل قائمة F5 (بدل الاكتفاء بأمر الأداة)
  - ملف جديد src/bfbc_configzip.h: قراءة وإعادة كتابة Render.ForceFov داخل
    config.zz بدون أي مكتبة ZIP خارجية.
      * ParseArchive/LoadArchive يقرآن سجل نهاية الدليل المركزي ثم سجلات
        الدليل المركزي (0x02014b50) مع أسماء المدخلات وأحجامها ومواضعها.
      * ReadEntry: method 0 يُنسخ مباشرة، وmethod 8 يُفكّ عبر
        stbi_zlib_decode_noheader_buffer المُصدَّر من rexruntime.dll (كتابة في
        مخزن المستدعي، فلا تخصيص عبر حدود الـDLL). الكاش لا يخزّن فشلًا،
        لأن الموديول قد لا يكون محمّلًا عند أول نداء.
      * WriteFov: كل مدخل غير Game.cfg يُنسخ حرفيًا بمنطقة الـlocal header
        (مع أي data descriptor)، وGame.cfg يُكتب غير مضغوط (method 0) بسطر
        Render.ForceFov داخل كتلة applySettings، مع CRC32 جديد ودليل مركزي
        وEOCD جديدين.
      * تحقق قبل لمس الملف الحقيقي: يُحلَّل المخزن الجديد من الذاكرة، وتُقارن
        كل المدخلات الأخرى بايت ببايت، ويُقرأ Game.cfg مرة أخرى ويُطابق النص
        المتوقع. ثم تُكتب نسخة .bfbc-backup مرة واحدة، والكتابة الفعلية عبر
        ملف .tmp ثم نسخه فوق الأصل.
  - F5 -> Rendering: Checkbox «Override field of view» + SliderInt 45..120،
    مع رسالة restart required. القيمة الحالية تُقرأ من config.zz عند فتح
    القائمة، و«Apply & save» يكتبها فقط إذا تغيّرت.
    (كان في القائمة زرّان: Apply يطبّق حيًّا بلا كتابة، وSave يكتب الملف؛
    أُدمجا في زر واحد لأن أي تعديل يُطبَّق بـApply كان يُفقد عند إعادة
    التشغيل. سبب ثانٍ لعدم الحفظ: كاتب config.zz كان يرفض كل كتابة بسبب
    خطأ في خطوة التحقق، وأُصلح.)
  - src/bfbc_graphics.h: بارامتر config_zz_path، وحقلا settings_.fov /
    stored_fov_، والقراءة في LoadFromCVars والكتابة في Apply.
  - src/bfbc_app.h: تمرير game_data_root()/dist/xeon/config.zz إلى القائمة.
  - البناء: build/bfbc.exe — نجح.
  - الاختبار (أداة مستقلة build/zip_test، حُذفت بعدها): 92 -> 78 -> off -> 85
    كلها قرأتها الأداة صحيحة، ثم فحص Python zipfile: testzip سليم، 51 مدخلًا
    بنفس الترتيب، لا مدخل تغيّر غير Game.cfg، وGame.cfg stored وحجمه 14119،
    وسطر واحد فقط لكل من begin/end، وإعدادات الظل وFilmGrain سليمة.

4) حذف قسمي «Lighting / Compatibility» و«Shadows / depth» من قائمة F5
  - طلب المستخدم: «شيل هذني مانحتاجهم بعد».
  - حُذف من src/bfbc_graphics.h: الدالتان DrawCompatibilitySection
    وDrawShadowSection ونداءاهما في OnDraw، وإعلاناهما، وحقول Settings
    الخاصة بهما، وقراءتها في LoadFromCVars، وإضافتها في PendingEntries
    (فلم تبق أي مراجع في الملف).
  - القيم التي كانت تكتبها بقيت في build/bfbc.toml (d3d12_readback_resolve،
    readback_resolve_half_pixel_offset، depth_float24_convert_in_pixel_shader
    = false، depth_float24_round = false، depth_transfer_not_equal_test،
    gpu_3d_to_2d_texture، texture_cache_memory_limit_render_to_texture = 24)
    لأن WriteConfigFile يبقي السطور التي لم تعد ضمن القائمة، وbfbc_app.h
    يدفعها قبل تهيئة الـGPU كما كان، فإصلاح الظلال والظلال الناعمة لم يتأثر.
  - البناء: build/bfbc.exe نجح، والنصّان "Lighting / Compatibility"
    و"Shadows / depth" لم يبق لهما أثر في الملف التنفيذي.

5) محرر الأزرار: التقاط بالضغط بدل كتابة النص
  - المشكلة: كل صف كان ImGui::InputText، وحقل النص لا يستقبل إلا الحروف المطبوعة،
    لذلك Shift/Ctrl/Alt (لا تنتج نصًا) والأسهم (تحرك المؤشر) وأزرار الماوس
    (لا تصل أصلًا) لم تكن قابلة للربط.
  - الحل: زر لكل ربط يعرض قيمته الحالية؛ بالضغط عليه يدخل الصف وضع الالتقاط،
    ثم تُقرأ حالة ImGui الخام: أزرار الماوس الثلاثة، وأي مفتاح من
    ImGuiKey_NamedKey_BEGIN..END عبر BindNameForImGuiKey، فيُكتب الاسم في
    keybind_* كما هو. Escape يلغي، و«Clear» يفرغ الربط، و«Apply bindings» كما هو.
  - الأسماء مأخوذة من جدول الأسماء داخل bin/rexruntime.dll (نفس ما يستخدمه
    ParseVirtualKey): A-Z، 0-9، F1-F12، Numpad0-9/NumpadEnter/NumpadPlus/
    NumpadMinus/NumpadStar/NumpadSlash، Up/Down/Left/Right، Shift، Control، Alt،
    Space، Return، Tab، Backspace، Delete، Insert، Home، End، PageUp، PageDown،
    Backtick، Minus، Plus، LBracket، RBracket، Backslash، Semicolon، Quote،
    Comma، Period، Slash، LMB، RMB، MMB.
  - حماية: إطار واحد يُتجاهل بعد نقرة بدء الالتقاط كي لا تُسجَّل النقرة نفسها.
  - البناء: build/bfbc.exe نجح، والنصّان "Keyboard / mouse bindings" و
    "Waiting for a key or mouse button" موجودان في الملف التنفيذي.

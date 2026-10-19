<p align="center">
  <img src="resources/Frame.png" width="128" height="128" alt="Affinity Arabic Logo" />
</p>

<h1 align="center">Affinity Arabic | تعريب أفينتي</h1>

<p align="center">
  <b>حل جذري لمشكلة تقطع الحروف وانعكاس النص العربي في برامج Affinity. بدلاً من الاعتماد على مواقع وسيطة أو النسخ واللصق، يقوم هذا المشروع بدمج دعم اللغة العربية داخل محرك البرنامج نفسه، لتتمكن من الكتابة والتعديل مباشرة وبشكل طبيعي كما لو أن البرنامج يدعمها من الأساس.</b>
</p>

<p align="center">
  <a href="https://www.buymeacoffee.com/Hayyan" target="_blank">
    <img src="https://img.shields.io/badge/Buy_Me_A_Coffee-FFDD00?style=for-the-badge&logo=buy-me-a-coffee&logoColor=black" alt="Buy Me A Coffee" />
  </a>
  <a href="https://x.com/HayyanCodes" target="_blank">
    <img src="https://img.shields.io/badge/X-@HayyanCodes-000000?style=for-the-badge&logo=x&logoColor=white" alt="Twitter" />
  </a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-0078D6?style=flat-square&logo=windows&logoColor=white" alt="Platform" />
  <img src="https://img.shields.io/badge/Architecture-x64-555555?style=flat-square" alt="Arch" />
  <img src="https://img.shields.io/badge/Affinity-v2-7B2CBF?style=flat-square" alt="Affinity" />
  <img src="https://img.shields.io/badge/License-GPL--3.0-blue?style=flat-square" alt="License" />
</p>

---

## المميزات

- **ربط الحروف تلقائياً**: تتصل الحروف ببعضها بأشكالها الصحيحة (في أول، وسط، أو آخر الكلمة) بالاعتماد على محرك OpenType في ويندوز، مع الحفاظ على المسافات والأبعاد الدقيقة لكل خط.
- **ضبط التشكيل والنقاط**: استقرار الحركات في مكانها الصحيح فوق وتحت الحروف دون تداخل، حتى مع الحروف المركبة والخطوط المعقدة.
- **دعم الخطوط المتغيرة (Variable Fonts)**: تحكم كامل في محاور الخط المخصصة مثل التمديد (الكشيدة) والاستطالة مباشرة من داخل البرنامج.
- **حركة مؤشر منطقية (BiDi)**: التحرك بالأسهم يميناً ويساراً يسير مع اتجاه القراءة الفعلي، مع دعم كامل للنصوص المختلطة (عربي/إنجليزي) في نفس السطر دون قفزات عشوائية للمؤشر.
- **التحكم بخصائص الخط (OpenType)**: إمكانية تفعيل أو تعطيل التراكيب الخاصة (`liga`)، البدائل السياقية (`calt`)، والوصلات الاختيارية (`dlig`) مباشرة من لوحة Typography.

> **ملاحظة تقنية:** لا يعتمد هذا الحل على عناوين ذاكرة ثابتة (Hardcoded Offsets)، بل يبحث عن الأنماط البرمجية (`Pattern Scanning` و `RTTI`) لحظة تشغيل البرنامج. هذا يعني أن الأداة ستستمر في العمل تلقائياً حتى بعد تحديث Affinity لإصدارات أحدث.

---

## طريقة التثبيت

لا يتطلب التثبيت أي خطوات معقدة أو نقل يدوي للملفات:

1. حمّل ملف التثبيت [`AffinityArabicSetup.exe`](https://github.com/Hayyan0/AffinityAr/releases/latest/download/AffinityArabicSetup.exe) وشغّله.
2. سيتعرف المثبّت تلقائياً على مسار برنامج Affinity في جهازك.
3. اضغط على **تثبيت**، ثم افتح Affinity كالمعتاد وستجد العربية تعمل معك مباشرة.

---

## متطلبات التشغيل

- نظام **Windows 10** أو **Windows 11** (64-bit).
- إصدار Affinity الجديد (الشامل).
- *(قريباً)*: يجري العمل حالياً على توفير نسخة متوافقة مع نظام Linux.

*ملاحظة: إذا كان برنامج Affinity مفتوحاً أثناء التثبيت، ستحتاج فقط لإعادة تشغيله لتفعيل الأداة.*

---

## التطوير والترخيص

تم تطوير هذا المشروع وهندسته بواسطة **حيّان (Hayyan)**.
- حسابي على منصة X: [@HayyanCodes](https://x.com/HayyanCodes)
- المشروع مفتوح المصدر ومرخص تحت رخصة [GPL-3.0](LICENSE).
- جميع الحقوق محفوظة © 2026 Hayyan.

---

## مكتبات الطرف الثالث (Third-Party Notices)

- **HDE64 (Hacker Disassembler Engine)**  
  Copyright © 2008-2009, Vyacheslav Patkov.  
  Licensed under the 2-Clause BSD License.
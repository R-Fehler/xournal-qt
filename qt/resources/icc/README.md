# sRGB.icc

The colour profile of the output intent of archive PDFs (PDF/A-3b, `qt/src/session/ArchivePdf.cpp`). It is compiled
into the app (`XqtSession.cmake` turns it into a byte array); nothing reads this file at run time.

- **What:** sRGB IEC61966-2.1, ICC version 2.2, display class, 3,268 bytes
  (SHA-256 `049e05b4b9f85e2b65d43e1bed89e301d157f14ba8dadefe3a92e12814d763da`).
- **Origin:** Graeme W. Gill's ArgyllCMS (`icc/sRGB.icm`), taken from TeX Live's `colorprofiles` package
  (`tex/generic/colorprofiles/sRGB.icc`, version 20181105).
- **License:** the profile's own copyright tag reads "Created by Graeme W. Gill. Released into the public domain. No
  Warranty, Use at your own risk." TeX Live's `colorprofiles` README lists it as "Copyright (c) 1997-2015 Graeme W.
  Gill" under the MIT (Expat) permission notice. Either way it is compatible with the GPL.

A version 2 profile was chosen over the ICC's sRGB v4 profile: every PDF/A part (and every PDF/A validator) accepts
version 2 profiles, and this one is small.

# Third-Party Notices

This mod is released under the MIT License, Copyright (c) 2026 itsloopyo. The
full text is in `LICENSE`, which ships at the root of every release ZIP.

The components below are third-party work with their own terms. This mod ships
a single installer ZIP and no Nexus ZIP, because its payload has to sit beside
`WolfNewOrder_x64.exe` where no mod manager can deploy it. That ZIP is a binary
distribution, so the notices required by those terms travel inside it alongside
`LICENSE`.

This repository contains no game code, no extracted game assets and no game
data files.

## Ultimate ASI Loader

- **Version:** v9.7.4 (commit `6b440669144c4a0bef5718ab155df160d231cd42`)
- **License:** MIT, Copyright (c) 2023 ThirteenAG
- **Upstream:** https://github.com/ThirteenAG/Ultimate-ASI-Loader
- **Usage:** proxy DLL that loads the mod's `.asi` into the game process. None
  of its code is linked into the mod; it is redistributed as an unmodified
  binary.
- **Bundled:** yes, as `vendor/ultimate-asi-loader/dinput8.dll`, with `LICENSE`
  and `README.md` beside it.

The upstream release asset `Ultimate-ASI-Loader_x64.zip` contains `dinput8.dll`
and nothing else, so the two files beside it come from elsewhere and are not
interchangeable. `LICENSE` is the upstream repository's licence text at the
pinned commit, which is the notice MIT requires us to carry. `README.md` is
ours, not upstream's: it records the asset, tag, commit and SHA-256 of the
binary actually on disk, and is the authority for those values. This entry is a
copy; if the two disagree, verify the hash and correct this file.

```
MIT License

Copyright (c) 2023 ThirteenAG

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

That `dinput8.dll` is a static binary and is not one component. The
`Ultimate-ASI-Loader-x64` target in `premake5.lua` at v9.7.4 compiles
`external/injector/minhook/src/**.c`,
`external/injector/utility/FunctionHookMinHook.cpp` and `external/miniz/miniz.c`
alongside the loader's own sources, so redistributing it redistributes MinHook,
injector and miniz as well, and each has its own section in this file.
MemoryModule, d3d8to9 and the minidx9 DirectX headers belong to the 32-bit
target only and are absent from this binary.

## MinHook

- **Version:** 1.3.4, the copy vendored at `cameraunlock-core/vendor/minhook`,
  plus the separate copy Ultimate ASI Loader v9.7.4 compiles from
  `external/injector/minhook/src/**.c`
- **License:** BSD-2-Clause
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** Function hooking. `CMakeLists.txt` builds
  `cameraunlock-core/vendor/minhook` and links it into the mod's `.asi`; that is
  how the camera hook is installed. The loader hooks with its own copy, compiled
  in by the `Ultimate-ASI-Loader-x64` target in `premake5.lua` at v9.7.4. The
  Hacker Disassembler Engine is compiled in alongside both - `hde64.c` in each,
  since MinHook's CMake and the loader's premake both pick the disassembler by
  architecture and this is an x64-only build - so the text below reproduces the
  HDE copyright the licence file carries alongside Tsuda Kageyu's. Both the
  HDE32 and HDE64 notices are reproduced because the licence file carries both.
- **Bundled:** yes. Compiled into the shipped `.asi`, and into the
  `dinput8.dll` beside it.

```
MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

================================================================================
Portions of this software are Copyright (c) 2008-2009, Vyacheslav Patkov.
================================================================================
Hacker Disassembler Engine 32 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-------------------------------------------------------------------------------
Hacker Disassembler Engine 64 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## injector

- **Version:** commit `3a384e8d1b575c09383b0fab8bd92e34cb654949`, the submodule
  Ultimate ASI Loader v9.7.4 pins at `external/injector/`
- **License:** zlib
- **Upstream:** https://github.com/ThirteenAG/injector
- **Usage:** The loader's `FunctionHookMinHook` wrapper, which the
  `Ultimate-ASI-Loader-x64` target compiles from
  `external/injector/utility/FunctionHookMinHook.cpp`, and the MinHook submodule
  that repository carries. Nothing in this repository calls or links it; it
  ships only inside that binary.
- **Bundled:** yes. Compiled into the shipped `dinput8.dll`.

The binary is unaltered upstream, so the "altered source versions" condition
below does not arise. It is reproduced whole regardless.

```
Copyright (C) 2012-2014 LINK/2012 <dma_2012@hotmail.com>

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
```

## miniz

- **Version:** 3.0.0, as vendored at `external/miniz/` in Ultimate ASI Loader
  v9.7.4
- **License:** MIT
- **Upstream:** https://github.com/richgel999/miniz
- **Usage:** Zip reading for the loader's `LoadVirtualFilesFromZip` path, which
  the `Ultimate-ASI-Loader-x64` target compiles from `external/miniz/miniz.c`.
  Nothing in this repository calls or links it; it ships only inside that
  binary.
- **Bundled:** yes. Compiled into the shipped `dinput8.dll`.

```
Copyright 2013-2014 RAD Game Tools and Valve Software
Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC

All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## cameraunlock-core

- **Version:** submodule at `def74d7107d1823340931cbc41e474cc652826f5`
- **License:** MIT, Copyright (c) 2026 itsloopyo
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** shared head tracking pipeline (UDP receiver, interpolation,
  smoothing, camera math), compiled into the shipped `.asi`.
- **Bundled:** its licence text ships in the installer ZIP as
  `licenses/cameraunlock-core-LICENSE.txt`. Being our own code is not an
  exemption: it carries its own LICENSE file, so MIT wants that notice in the
  distribution.

## OpenTrack

- **Version:** n/a (wire protocol only, no code used)
- **License:** ISC
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** This mod receives head pose over OpenTrack's UDP wire format. None
  of OpenTrack's code is linked or redistributed, so its ISC licence triggers no
  notice obligation; the entry credits the origin of the protocol.
- **Bundled:** no.

---

## Wolfenstein: The New Order footage

No game footage is in this repository today. The README embeds
`assets/readme-clip.gif`, that file has not been added yet, and the image
renders broken until it is. The terms below govern it from the moment it lands,
so nothing has to be remembered at that point.

- **File:** `assets/readme-clip.gif`, and nothing else. It is the single
  carve-out to the rule that no game material enters this repository, and it
  does not extend to screenshots, frame grabs or captures taken while reverse
  engineering.
- **Rights holder:** ZeniMax Media Inc. The game's own notice reads "(c) 2014
  ZeniMax Media Inc. Developed in association with MachineGames", so the
  copyright in the game, and so in any capture of it, is ZeniMax's; MachineGames
  developed it and Bethesda Softworks published it. The Wolfenstein mark and the
  stylised W are id Software LLC's. Add the holders of any third-party marks
  visible in frame.
- **Purpose:** one short gameplay capture at the top of the README, showing the
  mod running. Identification and illustration of the game this mod is for.
- **Bundled:** no. `scripts/package-release.ps1` stages named files only and
  never stages `assets/`, and the README embeds the clip by absolute URL, so the
  copy of the README inside the ZIP references it without carrying it.
- **Licence:** none is granted or implied over it. We will remove it on request
  from a rights holder.

If the clip is ever replaced with different footage, re-check the rights-holder
line against what is actually in frame.

Naming Wolfenstein: The New Order, MachineGames, Bethesda Softworks, ZeniMax
Media and id Software in this repository is nominative use to identify the game
the mod is for. Wolfenstein and the stylised W are trademarks of id Software
LLC; MachineGames, Bethesda, Bethesda Softworks and ZeniMax are trademarks of
ZeniMax Media Inc. This mod is not affiliated with, endorsed by or supported by
any of them.

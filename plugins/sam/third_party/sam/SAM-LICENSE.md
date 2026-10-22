# SAM (Software Automatic Mouth): provenance and licence status

The C files in this directory are a copy of `~/github/SAM/src/`, a clone of
<https://github.com/vidarh/SAM> (a descendant of Sebastian Macke's C conversion,
<https://github.com/s-macke/SAM>). The commit was not recorded when the copy was
taken. Local changes are listed in `PATCHES.md`; everything else is as found.

SAM was published in 1982 for the Commodore 64 by Don't Ask Software, now SoftVoice, Inc.
The C code is a reverse-engineered conversion of that program.

**There is no licence.** The upstream README says:

> The software is a reverse-engineered version of a commercial software published more
> than 30 years ago. The current copyright holder is SoftVoice, Inc. (www.text2speech.com)
>
> Any attempt to contact the company failed. The website was last updated in the year 2009.
> The status of the original software can therefore best described as Abandonware.
>
> As long this is the case I cannot put my code under any specific open source software
> license. Use it at your own risk.

The rest of Downspout is MIT. This directory is not covered by that licence, and nothing here
grants permission to redistribute it. Whether the `sam.vst3` bundle may be shipped in public
releases is a decision for the repository owner. To build without it, configure with
`-DDOWNSPOUT_BUILD_SAM=OFF` (and drop `sam.vst3` from the bundle lists the release scripts check).

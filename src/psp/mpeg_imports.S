	.set noreorder
#include "pspimport.s"

// sceMpeg and sceMpegbase as the XMB's mpeg_vsh.prx exports them (1.3.3: H.264 in MP4, UMD Video's PSMF). One object per
// library: split stubs break linking (see net_imports.S). These resolve only once Nemo loads
// flash0:/kd/mpegbase_260.prx and flash0:/kd/mpeg_vsh.prx; nothing may call them before.
// NIDs: cooleyes' avcdecoder sample (2009, GPL-2.0-or-later).

IMPORT_START "sceMpeg",0x00090000
IMPORT_FUNC  "sceMpeg",0x682A619B,sceMpegInit
IMPORT_FUNC  "sceMpeg",0x874624D6,sceMpegFinish
IMPORT_FUNC  "sceMpeg",0xC132E22F,sceMpegQueryMemSize
IMPORT_FUNC  "sceMpeg",0xD8C5F121,sceMpegCreate
IMPORT_FUNC  "sceMpeg",0x606A4649,sceMpegDelete
IMPORT_FUNC  "sceMpeg",0x167AFD9E,sceMpegInitAu
IMPORT_FUNC  "sceMpeg",0x0E3C2E9D,sceMpegAvcDecode
IMPORT_FUNC  "sceMpeg",0xA11C7026,sceMpegAvcDecodeMode
IMPORT_FUNC  "sceMpeg",0xCF3547A2,sceMpegAvcDecodeDetail2
IMPORT_FUNC  "sceMpeg",0x11F95CF1,sceMpegGetAvcNalAu
IMPORT_FUNC  "sceMpeg",0xD7A29F46,sceMpegRingbufferQueryMemSize
IMPORT_FUNC  "sceMpeg",0x37295ED8,sceMpegRingbufferConstruct
IMPORT_FUNC  "sceMpeg",0x13407F13,sceMpegRingbufferDestruct
IMPORT_FUNC  "sceMpeg",0xB240A59E,sceMpegRingbufferPut
IMPORT_FUNC  "sceMpeg",0xB5F6DC87,sceMpegRingbufferAvailableSize
IMPORT_FUNC  "sceMpeg",0x42560F23,sceMpegRegistStream
IMPORT_FUNC  "sceMpeg",0x591A4AA2,sceMpegUnRegistStream
IMPORT_FUNC  "sceMpeg",0xA780CF7E,sceMpegMallocAvcEsBuf
IMPORT_FUNC  "sceMpeg",0xCEB870B1,sceMpegFreeAvcEsBuf
IMPORT_FUNC  "sceMpeg",0xF8DCB679,sceMpegQueryAtracEsSize
IMPORT_FUNC  "sceMpeg",0xFE246728,sceMpegGetAvcAu
IMPORT_FUNC  "sceMpeg",0xE1CE83A7,sceMpegGetAtracAu
IMPORT_FUNC  "sceMpeg",0x800C44DF,sceMpegAtracDecode
IMPORT_FUNC  "sceMpeg",0x707B7629,sceMpegFlushAllStream

IMPORT_START "sceMpegbase",0x00090000
IMPORT_FUNC  "sceMpegbase",0x91929A21,sceMpegBaseCscAvc

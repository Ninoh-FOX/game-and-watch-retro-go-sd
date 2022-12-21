/****************************************************************************
 *  Caprice32 libretro port
 *
 *  Copyright David Colmenero - D_Skywalk (2019-2021)
 *
 *  Redistribution and use of this code or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *   - Redistributions may not be sold, nor may they be used in a commercial
 *     product or activity.
 *
 *   - Redistributions that are modified from the original source must include the
 *     complete source code, including the source code for all components used by a
 *     binary built from the modified sources. However, as a special exception, the
 *     source code distributed need not include anything that is normally distributed
 *     (in either source or binary form) with the major components (compiler, kernel,
 *     and so on) of the operating system on which the executable runs, unless that
 *     component itself accompanies the executable.
 *
 *   - Redistributions must reproduce the above copyright notice, this list of
 *     conditions and the following disclaimer in the documentation and/or other
 *     materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************************/

#include "cap32.h"

extern t_GateArray GateArray;
extern uint32_t amstrad_colours[32];

#define RGB2COLOR(r, g, b)    ((((int)(r) >> 5) << 5) | (((int)(g) >> 5) << 2) | ((int)(b) >> 6))
#define RGB2RED(colour)       (((colour>>5)<<5) & 0xFF)
#define RGB2GREEN(colour)     (((colour>>2)<<5) & 0xFF)
#define RGB2BLUE(colour)      ((colour<<6) & 0xFF)

/**
 * generate antialias values using 8bpp macros
 *
 */
void video_set_palette_antialias_8bpp(void)
{
   uint8_t r2,g2,b2;
   r2=RGB2RED(amstrad_colours[GateArray.ink_values[0]]) + RGB2RED(amstrad_colours[GateArray.ink_values[1]]);
   g2=RGB2GREEN(amstrad_colours[GateArray.ink_values[0]]) + RGB2GREEN(amstrad_colours[GateArray.ink_values[1]]);
   b2=RGB2BLUE(amstrad_colours[GateArray.ink_values[0]]) + RGB2BLUE(amstrad_colours[GateArray.ink_values[1]]);
   GateArray.palette[33] = (unsigned short) RGB2COLOR(r2/2, g2/2, b2/2);
}

/**
 * rgb2color_8bpp:
 * convert rgb to 8bpp color (see macros)
 **/
unsigned int rgb2color_8bpp(unsigned int r, unsigned int g, unsigned int b)
{
   return RGB2COLOR(r, g, b);
}

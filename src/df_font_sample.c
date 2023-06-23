/*
   This file is part of DirectFB-media-samples.

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
*/

#include <direct/util.h>
#include <directfb.h>

/* macro for a safe call to DirectFB functions */
#define DFBCHECK(x)                                                   \
     do {                                                             \
          DFBResult ret = x;                                          \
          if (ret != DFB_OK) {                                        \
               fprintf( stderr, "%s <%d>:\n\t", __FILE__, __LINE__ ); \
               DirectFBErrorFatal( #x, ret );                         \
          }                                                           \
     } while (0)

/* DirectFB interfaces */
static IDirectFB             *dfb         = NULL;
static IDirectFBDisplayLayer *layer       = NULL;
static IDirectFBWindow       *window       = NULL;
static IDirectFBSurface      *surface      = NULL;
static IDirectFBEventBuffer  *event_buffer = NULL;

/* list of font files */
static char **fontfile_list;
static int    fontfile_count;

/* command line options */
static int width  = 0;
static int height = 0;

/**********************************************************************************************************************/

static int show_ascender     = 0;
static int show_descender    = 0;
static int show_baseline     = 0;
static int show_glyphrect    = 0;
static int show_glyphadvance = 0;
static int show_glyphorigin  = 0;

static int antialias         = 1;
static int unicode_mode      = 1;

static int show_help         = 0;

static int glyphs_per_xline  = 16;
static int glyphs_per_yline  = 16;

#define GLYPHS_PER_PAGE (glyphs_per_xline * glyphs_per_yline)

/**********************************************************************************************************************/

static const struct {
     char *key;
     char *description;
} key_description [] = {
     { "PGUP",       "page up"},
     { "PGDOWN",     "page down"},
     { "A",          "show/hide Ascender"},
     { "D",          "show/hide Descender"},
     { "B",          "show/hide Baseline"},
     { "R",          "show/hide Glyph Rectangle"},
     { "G",          "show/hide Glyph Advance"},
     { "O",          "show/hide Glyph Origin"},
     { "SPC/UP",     "next Font"},
     { "BKSPC/DOWN", "prev Font"},
     { "PLUS",       "more Glyphs per Page"},
     { "MINUS",      "less Glyphs per Page"},
     { "U",          "toggle Unicode/Raw Glyph Map"},
     { "M",          "enable/disable Antialiasing"},
     { "F1",         "Help"},
     { "ESC",        "Exit"}
};

static void render_help_page( const char *fontfile )
{
     DFBFontDescription  fdsc;
     IDirectFBFont      *fixedfont;
     int                 i;

     /* load fixedfont */
     fdsc.flags  = DFDESC_HEIGHT;
     fdsc.height = 16;

     DFBCHECK(dfb->CreateFont( dfb, fontfile, &fdsc, &fixedfont ));

     surface->SetColor( surface, 0x00, 0x00, 0x00, 0xff );
     surface->SetFont( surface, fixedfont );

     for (i = 0; i < D_ARRAY_SIZE(key_description); i++) {
          int x = 150 + (i / ((D_ARRAY_SIZE(key_description) + 1) / 2)) * (width - 100) / 2;
          int y = 60  + (i % ((D_ARRAY_SIZE(key_description) + 1) / 2)) * 25;

          surface->DrawString( surface, key_description[i].key, -1, x - 10, y, DSTF_RIGHT );
          surface->DrawString( surface, key_description[i].description, -1, x + 10, y, DSTF_LEFT );
     }

     surface->DrawString( surface, "Loaded Fonts:", -1, width / 2, 300, DSTF_CENTER );

     for (i = 0; i < fontfile_count; i++)
          surface->DrawString( surface, fontfile_list[i], -1, width / 2, 340 + i * 20, DSTF_CENTER );

     fixedfont->Release( fixedfont );
}

static void render_font_page( const char *fontfile, unsigned int first_char )
{
     DFBFontDescription  fdsc;
     IDirectFBFont      *font, *fixedfont;
     int                 bwidth, bheight;
     int                 xborder, yborder;
     int                 ascender, descender;
     int                 baseoffset;
     int                 i, j;
     char                label[32];

     bwidth  = width * 7 / 8;
     bheight = height * 7 / 8;

     xborder = (width - bwidth) / 2;
     yborder = (height - bheight) / 2;

     /* load fixedfont */
     fdsc.flags  = DFDESC_HEIGHT;
     fdsc.height = 16;

     DFBCHECK(dfb->CreateFont( dfb, fontfile, &fdsc, &fixedfont ));

     surface->SetFont( surface, fixedfont );

     /* load font */
     if (!strstr( fontfile, ".dgiff" )) {
          fdsc.flags      |= DFDESC_ATTRIBUTES;
          fdsc.height      = 9 * bheight / glyphs_per_yline / 16;
          fdsc.attributes  = antialias ? 0 : DFFA_MONOCHROME;
          fdsc.attributes |= unicode_mode ? 0 : DFFA_NOCHARMAP;
     }

     if (dfb->CreateFont( dfb, fontfile, &fdsc, &font ) != DFB_OK) {
          static const char *msg = "failed opening '";
          char               text[strlen( msg ) + strlen( fontfile ) + 2];

          strcpy( text, msg );
          strcpy( text + strlen( msg ), fontfile );
          strcpy( text + strlen( msg ) + strlen( fontfile ), "'" );

          surface->SetColor( surface, 0xff, 0x00, 0x00, 0xff );
          surface->DrawString( surface, text, -1, width / 2, 10, DSTF_TOPCENTER );

          fixedfont->Release( fixedfont );

          return;
     }

     DFBCHECK(font->GetAscender( font, &ascender ));
     DFBCHECK(font->GetDescender( font, &descender ));

     baseoffset = (bheight / glyphs_per_yline - (ascender - descender)) / 2 + ascender;

     surface->SetColor( surface, 0xa0, 0xa0, 0xa0, 0xff );

     surface->DrawString( surface, fontfile, -1, width / 2, 10, DSTF_TOPCENTER );

     surface->DrawString( surface, unicode_mode ? "Unicode Map" : "Raw Map", -1, 10, 10, DSTF_TOPLEFT );

     snprintf( label, sizeof(label), "%d pixels", fdsc.height );

     surface->DrawString( surface, label, -1, width - 10, 10, DSTF_TOPRIGHT );

     surface->DrawString( surface, "Press F1 for Help", -1, width / 2, height - 15, DSTF_CENTER );

     surface->SetColor( surface, 0xc0, 0xc0, 0xc0, 0xff );

     for (j = 0; j < glyphs_per_yline; j++) {
          int basey;

          basey = j * bheight / glyphs_per_yline + yborder + baseoffset;

          snprintf( label, sizeof(label), "%04x", first_char + j * glyphs_per_xline );

          surface->DrawString( surface, label, -1, xborder - 10, basey, DSTF_RIGHT );

          snprintf( label, sizeof(label), "%04x", first_char + (j + 1) * glyphs_per_yline - 1 );

          surface->DrawString( surface, label, -1, bwidth + xborder + 10, basey, DSTF_LEFT );
     }

     fixedfont->Release( fixedfont );

     for (i = 0; i <= glyphs_per_xline; i++) {
          int basex;

          basex = i * bwidth / glyphs_per_xline + xborder;

          surface->DrawLine( surface, basex, yborder, basex, bheight + yborder );
     }

     for (j = 0; j <= glyphs_per_yline; j++) {
          int basey;

          basey = j * bheight / glyphs_per_yline + yborder;

          surface->DrawLine( surface, xborder, basey, bwidth + xborder, basey );
     }

     if (show_ascender) {
          surface->SetColor( surface, 0xf0, 0x80, 0x80, 0xff );

          for (j = 0; j < glyphs_per_yline; j++) {
               int basey;

               basey = j * bheight / glyphs_per_yline + yborder + baseoffset;

               surface->DrawLine( surface, xborder, basey - ascender, bwidth + xborder, basey - ascender );
          }
     }

     if (show_descender) {
          surface->SetColor( surface, 0x80, 0xf0, 0x80, 0xff );

          for (j = 0; j < glyphs_per_yline; j++) {
               int basey;

               basey = j * bheight / glyphs_per_yline + yborder + baseoffset;

               surface->DrawLine( surface, xborder, basey - descender, bwidth + xborder, basey - descender );
          }
     }

     if (show_baseline) {
          surface->SetColor( surface, 0x80, 0x80, 0xf0, 0xff );

          for (j = 0; j < glyphs_per_yline; j++) {
               int basey;

               basey = j * bheight / glyphs_per_yline + yborder + baseoffset;

               surface->DrawLine( surface, xborder, basey, bwidth + xborder, basey );
          }
     }

     surface->SetFont( surface, font );

     for (j = 0; j < glyphs_per_yline; j++) {
          for (i = 0; i < glyphs_per_xline; i++) {
               int          basex;
               int          basey;
               int          glyphindex;
               int          glyphadvance;
               DFBRectangle glyphrect;

               basex = (2 * i + 1) * bwidth / glyphs_per_xline / 2 + xborder;
               basey = j * bheight / glyphs_per_yline + yborder + baseoffset;

               glyphindex = first_char + i + j * glyphs_per_xline;

               DFBCHECK(font->GetGlyphExtents( font, glyphindex, &glyphrect, &glyphadvance ));

               if (show_glyphrect) {
                    int x = basex + glyphrect.x - glyphrect.w / 2;
                    int y = basey + glyphrect.y;

                    surface->SetColor( surface, 0xc0, 0xc0, 0xf0, 0xff );
                    surface->FillRectangle( surface, x, y, glyphrect.w, glyphrect.h );
               }

               if (show_glyphadvance) {
                    int y = (j + 1) * bheight / glyphs_per_yline + yborder - 4;

                    surface->SetColor( surface, 0x30, 0xc0, 0x30, 0xff );
                    surface->FillRectangle( surface, basex - glyphrect.w / 2, y, glyphadvance, 3 );
               }

               surface->SetColor( surface, 0x00, 0x00, 0x00, 0xff );
               surface->DrawGlyph( surface, glyphindex, basex - glyphrect.w / 2, basey, DSTF_LEFT );

               if (show_glyphorigin) {
                    surface->SetColor( surface, 0xff, 0x30, 0x30, 0xff );
                    surface->FillRectangle( surface, basex - 1, basey - 1, 2, 2 );
               }
          }
     }

     font->Release( font );
}

/**********************************************************************************************************************/

static void dfb_shutdown()
{
     if (event_buffer) event_buffer->Release( event_buffer );
     if (surface)      surface->Release( surface );
     if (window)       window->Release( window );
     if (layer)        layer->Release( layer );
     if (dfb)          dfb->Release( dfb );
}

static void print_usage()
{
     printf( "DirectFB Font Sample Viewer\n\n" );
     printf( "Usage: df_font_sample [options] files\n\n" );
     printf( "Options:\n\n" );
     printf( "  --size=<width>x<height>  Set windows size.\n" );
     printf( "  --help                   Print usage information.\n" );
     printf( "  --dfb-help               Output DirectFB usage information.\n\n" );
     printf( "Use:\n" );
     printf( "  ESC,Q,q           to quit\n" );
     printf( "  F1,H,h            to show help\n" );
     printf( "  Space,up          to show next font\n" );
     printf( "  Backspace,down    to show prev font\n" );
     printf( "  page up,page down to show next/prev page\n" );
     printf( "  +,-               to increase/decrease the number of glyphs per page\n" );
     printf( "  A,a               to show/hide ascender\n" );
     printf( "  B,b               to show/hide baseline\n" );
     printf( "  D,d               to show/hide descender\n" );
     printf( "  G,g               to show/hide glyph advance\n" );
     printf( "  M,m               to enable/disable antialiasing\n" );
     printf( "  O,o               to show/hide glyph origin\n" );
     printf( "  R,r               to show/hide glyph rectangle\n" );
     printf( "  U,u               to toggle Unicode/Raw glyph map\n" );
}

int main( int argc, char *argv[] )
{
     DFBDisplayLayerConfig config;
     DFBWindowDescription  wdsc;
     int                   i;
     int                   update       = 1;
     int                   first_glyph  = 0;
     int                   current_font = 0;

     /* initialize DirectFB including command line parsing */
     DFBCHECK(DirectFBInit( &argc, &argv ));

     /* parse command line */
     for (i = 1; i < argc; i++) {
          char *option = argv[i];

          if (*option == '-') {
               option++;

               if (!strcmp( option, "-help" )) {
                    print_usage();
                    return 0;
               } else
               if (!strncmp( option, "-size=", sizeof("-size=") - 1 )) {
                    option += sizeof("-size=") - 1;
                    sscanf( option, "%dx%d", &width, &height );
               }
          }
          else {
               fontfile_count = argc - i;
               fontfile_list  = argv + i;
               break;
          }
     }

     if (!fontfile_count) {
          print_usage();
          return 1;
     }

     /* create the main interface */
     DFBCHECK(DirectFBCreate( &dfb ));

     /* register termination function */
     atexit( dfb_shutdown );

     /* get the primary display layer */
     DFBCHECK(dfb->GetDisplayLayer( dfb, DLID_PRIMARY, &layer ));

     DFBCHECK(layer->GetConfiguration( layer, &config ));

     width  = width  ?: config.width;
     height = height ?: config.height;

     /* create the window */
     wdsc.flags  = DWDESC_POSX | DWDESC_POSY | DWDESC_WIDTH | DWDESC_HEIGHT;
     wdsc.posx   = 0;
     wdsc.posy   = 0;
     wdsc.width  = width;
     wdsc.height = height;

     DFBCHECK(layer->CreateWindow( layer, &wdsc, &window ));
     DFBCHECK(window->GetSurface( window, &surface ));

     /* create an event buffer */
     DFBCHECK(window->CreateEventBuffer( window, &event_buffer ));

     window->SetOpacity( window, 0xff );
     window->RequestFocus( window );

     /* main loop */
     while (1) {
          DFBWindowEvent  evt;
          char           *fontfile = fontfile_list[current_font];

          if (update) {
               surface->Clear( surface, 0xff, 0xff, 0xff, 0xff );

               if (show_help)
                    render_help_page( fontfile );
               else
                    render_font_page( fontfile, first_glyph );

               surface->Flip( surface, NULL, DSFLIP_WAITFORSYNC );

               update = 0;
          }

          event_buffer->WaitForEvent( event_buffer );

          /* process event buffer */
          while (event_buffer->GetEvent( event_buffer, DFB_EVENT(&evt) ) == DFB_OK) {
               if (evt.type == DWET_KEYUP) {
                    if (show_help) {
                         show_help = 0;
                         update = 1;
                    }
               }
               else if (evt.type == DWET_KEYDOWN) {
                    switch (DFB_LOWER_CASE( evt.key_symbol )) {
                         case DIKS_ESCAPE:
                         case DIKS_SMALL_Q:
                         case DIKS_BACK:
                         case DIKS_STOP:
                         case DIKS_EXIT:
                              return 42;

                         case DIKS_PAGE_DOWN:
                         case DIKS_CURSOR_RIGHT:
                              first_glyph += GLYPHS_PER_PAGE;
                              if (first_glyph > 0xffff)
                                   first_glyph = 0;
                              update = 1;
                              break;

                         case DIKS_PAGE_UP:
                         case DIKS_CURSOR_LEFT:
                              first_glyph -= GLYPHS_PER_PAGE;
                              if (first_glyph < 0x0000)
                                   first_glyph = 0x10000 - GLYPHS_PER_PAGE;
                              update = 1;
                              break;

                         case DIKS_SPACE:
                         case DIKS_CURSOR_UP:
                              if (++current_font >= fontfile_count)
                                   current_font = 0;
                              update = 1;
                              break;

                         case DIKS_BACKSPACE:
                         case DIKS_CURSOR_DOWN:
                              if (--current_font < 0)
                                   current_font = fontfile_count - 1;
                              update = 1;
                              break;

                         case 'a':
                              show_ascender = !show_ascender;
                              update = 1;
                              break;

                         case 'd':
                              show_descender = !show_descender;
                              update = 1;
                              break;

                         case 'b':
                              show_baseline = !show_baseline;
                              update = 1;
                              break;

                         case 'r':
                              show_glyphrect = !show_glyphrect;
                              update = 1;
                              break;

                         case 'g':
                              show_glyphadvance = !show_glyphadvance;
                              update = 1;
                              break;

                         case 'o':
                              show_glyphorigin = !show_glyphorigin;
                              update = 1;
                              break;

                         case 'm':
                              antialias = !antialias;
                              update = 1;
                              break;

                         case 'u':
                              unicode_mode = !unicode_mode;
                              update = 1;
                              break;

                         case 'h':
                         case DIKS_F1:
                         case DIKS_HELP:
                              if (!show_help) {
                                   show_help = 1;
                                   update = 1;
                              }
                              break;

                         case DIKS_MINUS_SIGN:
                              if (glyphs_per_xline > 1)
                                   glyphs_per_xline--;
                              if (glyphs_per_yline > 1)
                                   glyphs_per_yline--;
                              update = 1;
                              break;

                         case DIKS_PLUS_SIGN:
                              glyphs_per_xline++;
                              glyphs_per_yline++;
                              update = 1;
                              break;

                         default:
                              break;
                    }
               }
          }
     }

     /* shouldn't reach this */
     return 0;
}

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

#include <direct/hash.h>
#include <directfb.h>

#include "tinylogo.h"

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
static IDirectFB              *dfb          = NULL;
static IDirectFBDisplayLayer  *layer        = NULL;
static IDirectFBEventBuffer   *event_buffer = NULL;
static IDirectFBSurface       *logo         = NULL;

/* window hash table */
static DirectHash *window_stack = NULL;

/* list of image files */
static char **mrl_list;
static int    mrl_count;

/* command line options */
static int info       = 0;
static int use_logo   = 1;
static int win_width  = 0;
static int win_height = 0;

/* logo color */
static DFBColor logo_color = { 0xbb, 0x33, 0x22, 0xff };

/**********************************************************************************************************************/

static void render_func( IDirectFBSurface *surface )
{
     if (logo) {
          surface->SetColor( surface, logo_color.r, logo_color.g, logo_color.b, 0xff );
          surface->SetBlittingFlags( surface, DSBLIT_COLORIZE | DSBLIT_BLEND_ALPHACHANNEL );
          surface->Blit( surface, logo, NULL, 5, 5 );
     }

     surface->Flip( surface, NULL, DSFLIP_NONE );
}

/**********************************************************************************************************************/

static void add_window( IDirectFBImageProvider *image_provider, DFBSurfaceDescription *sdsc )
{
     DFBWindowID           id;
     DFBWindowDescription  wdsc;
     IDirectFBWindow      *window;
     IDirectFBSurface     *surface;

     wdsc.flags  = DWDESC_POSX | DWDESC_POSY | DWDESC_WIDTH | DWDESC_HEIGHT;
     wdsc.posx   = 32 * direct_hash_count( window_stack );
     wdsc.posy   = 18 * direct_hash_count( window_stack );
     wdsc.width  = win_width  ?: sdsc->width;
     wdsc.height = win_height ?: sdsc->height;

     DFBCHECK(layer->CreateWindow( layer, &wdsc, &window ));
     DFBCHECK(window->GetSurface( window, &surface ));
     DFBCHECK(window->AttachEventBuffer( window, event_buffer ));

     surface->Clear( surface, 0x00, 0x00, 0x00, 0xff );
     surface->Flip( surface, NULL, DSFLIP_NONE );

     window->GetID( window, &id );
     window->SetOpacity( window, 0xff );
     window->RequestFocus( window );

     direct_hash_insert( window_stack, id, window );

     /* render the image */
     image_provider->RenderTo( image_provider, surface, NULL );

     render_func( surface );

     surface->Release( surface );
}

static bool remove_window( DFBWindowID id )
{
     IDirectFBWindow *window = direct_hash_lookup( window_stack, id );

     if (window) {
          window->Release( window );
          direct_hash_remove( window_stack, id );
          return true;
     }
     else
          return false;
}

static bool stack_destructor( DirectHash *stack, unsigned long id, void *value, void *ctx )
{
     IDirectFBWindow *window = value;

     window->Release( window );

     return true;
}

/**********************************************************************************************************************/

static void dfb_shutdown()
{
     if (window_stack) {
          direct_hash_iterate( window_stack, stack_destructor, NULL );
          direct_hash_destroy( window_stack );
     }

     if (logo)         logo->Release( logo );
     if (event_buffer) event_buffer->Release( event_buffer );
     if (layer)        layer->Release( layer );
     if (dfb)          dfb->Release( dfb );
}

static void print_usage()
{
     printf( "DirectFB Image Sample Viewer\n\n" );
     printf( "Usage: df_image_sample [options] files\n\n" );
     printf( "Options:\n\n" );
     printf( "  --info                   Dump image info.\n" );
     printf( "  --no-logo                Do not display DirectFB logo in the upper-left corner of the window.\n" );
     printf( "  --size=<width>x<height>  Set windows size.\n" );
     printf( "  --help                   Print usage information.\n" );
     printf( "  --dfb-help               Output DirectFB usage information.\n\n" );
     printf( "Use:\n" );
     printf( "  ESC,Q,q to quit\n" );
}

int main( int argc, char *argv[] )
{
     int i;

     if (argc < 2) {
          print_usage();
          return 0;
     }

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
               if (!strcmp( option, "-info" )) {
                    info = 1;
               } else
               if (!strcmp( option, "-no-logo" )) {
                    use_logo = 0;
               } else
               if (!strncmp( option, "-size=", sizeof("-size=") - 1 )) {
                    option += sizeof("-size=") - 1;
                    sscanf( option, "%dx%d", &win_width, &win_height );
               }
          }
          else {
               mrl_count = argc - i;
               mrl_list  = argv + i;
               break;
          }
     }

     if (!mrl_count) {
          print_usage();
          return 1;
     }

     /* create the main interface */
     DFBCHECK(DirectFBCreate( &dfb ));

     /* register termination function */
     atexit( dfb_shutdown );

     /* get the primary display layer */
     DFBCHECK(dfb->GetDisplayLayer( dfb, DLID_PRIMARY, &layer ));

     /* create an event buffer */
     DFBCHECK(dfb->CreateEventBuffer( dfb, &event_buffer ));

     /* create logo */
     if (use_logo)
          DFBCHECK(dfb->CreateSurface( dfb, &tinylogo_desc, &logo ));

     /* create window stack */
     direct_hash_create( mrl_count, &window_stack );

     for (i = 0; i < mrl_count; i++) {
          DFBSurfaceDescription   sdsc;
          IDirectFBImageProvider *image_provider;

          /* create an image provider */
          DFBCHECK(dfb->CreateImageProvider( dfb, mrl_list[i], &image_provider ));

          /* retrieve a surface description of the image */
          image_provider->GetSurfaceDescription( image_provider, &sdsc );

          /* dump image information */
          if (info) {
               DFBImageDescription desc;

               printf( "%s\n", mrl_list[i] );
               printf( "  # Image: %dx%d\n", sdsc.width, sdsc.height );

               image_provider->GetImageDescription( image_provider, &desc );

               if (desc.caps & DICAPS_COLORKEY)
                    printf( "  # Color key: 0x%x 0x%x 0x%x\n", desc.colorkey_r, desc.colorkey_g, desc.colorkey_b );
          }

          /* add window to the stack */
          add_window( image_provider, &sdsc );

          image_provider->Release( image_provider );
     }

     /* main loop */
     while (1) {
          DFBWindowEvent evt;

          event_buffer->WaitForEvent( event_buffer );

          /* process event buffer */
          while (event_buffer->GetEvent( event_buffer, DFB_EVENT(&evt) ) == DFB_OK) {
               switch (evt.type) {
                    case DWET_KEYDOWN:
                         switch (DFB_LOWER_CASE( evt.key_symbol )) {
                              case DIKS_ESCAPE:
                              case DIKS_SMALL_Q:
                              case DIKS_BACK:
                              case DIKS_STOP:
                              case DIKS_EXIT:
                                   return 42;

                              default:
                                   break;
                         }
                         break;

                    case DWET_CLOSE: {
                         if (remove_window( evt.window_id )) {
                              if (--mrl_count <= 0)
                                   return 42;
                         }
                         break;
                    }

                    default:
                         break;
               }
          }
     }

     /* shouldn't reach this */
     return 0;
}

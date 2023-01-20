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
#include <direct/util.h>
#include <directfb.h>
#include <directfb_util.h>

#include "dfblogo.h"

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
static IDirectFBImageProvider *image        = NULL;
static IDirectFBSurface       *frame        = NULL;
static IDirectFBSurface       *logo         = NULL;

/* window struct */
struct stack_entry {
     IDirectFBWindow  *window;
     IDirectFBSurface *surface;
};

/* window hash table */
static DirectHash  *window_stack = NULL;

/* command line options */
static int                   info        = 0;
static int                   use_logo    = 1;
static DFBSurfacePixelFormat pixelformat = DSPF_UNKNOWN;
static int                   width       = 0;
static int                   height      = 0;
static int                   n_windows   = 1;

/******************************************************************************/

static void dump_image_info( DFBSurfaceDescription *dsc )
{
     DFBImageDescription desc;

     image->GetImageDescription( image, &desc );

     printf( "  # Image: %dx%d", dsc->width, dsc->height );

     if (desc.caps & DICAPS_COLORKEY) {
          printf( " (colorkey 0x%x 0x%x 0x%x)", desc.colorkey_r, desc.colorkey_g, desc.colorkey_b );
     }

     printf( "\n" );
}

/******************************************************************************/

static void create_stack( DFBSurfaceDescription *dsc )
{
     DFBWindowDescription wdsc;
     int                  i;

     /* create hash table */
     direct_hash_create( n_windows, &window_stack );

     wdsc.flags  = DWDESC_POSX | DWDESC_POSY | DWDESC_WIDTH | DWDESC_HEIGHT;
     wdsc.posx   = 0;
     wdsc.posy   = 0;
     wdsc.width  = width  = width  ?: dsc->width;
     wdsc.height = height = height ?: dsc->height;

     for (i = 0; i < n_windows; i++, wdsc.posx += 10, wdsc.posy += 5) {
          struct stack_entry *entry;
          IDirectFBWindow    *window;
          IDirectFBSurface   *surface;
          DFBWindowID         id;

          entry = calloc( 1, sizeof(struct stack_entry) );

          DFBCHECK(layer->CreateWindow( layer, &wdsc, &window ));
          DFBCHECK(window->GetSurface( window, &surface ));
          DFBCHECK(window->AttachEventBuffer( window, event_buffer ));

          window->GetID( window, &id );
          window->SetOpacity( window, 0xff );

          surface->Clear( surface, 0x00, 0x00, 0x00, 0xff );
          surface->Flip( surface, NULL, DSFLIP_NONE );

          entry->window  = window;
          entry->surface = surface;
          direct_hash_insert( window_stack, id, entry );
     }
}

static IDirectFBWindow *remove_window( DFBWindowID id )
{
     struct stack_entry *entry;
     IDirectFBWindow    *window = NULL;

     entry = direct_hash_lookup( window_stack, id );
     if (entry) {
          window = entry->window;
          entry->surface->Release( entry->surface );
          window->Release( window );
          free( entry );
          direct_hash_remove( window_stack, id );
     }

     return window;
}

static bool stack_destructor( DirectHash *stack, unsigned long id, void *value, void *ctx )
{
     struct stack_entry *entry = value;

     entry->surface->Release( entry->surface );
     entry->window->Release( entry->window );
     free( entry );

     return true;
}

static void destroy_stack()
{
     direct_hash_iterate( window_stack, stack_destructor, NULL );

     /* destroy hash table */
     direct_hash_destroy( window_stack );
}

/******************************************************************************/

static void create_frame( DFBSurfaceDescription *dsc )
{
     DFBSurfaceDescription sdsc;

     sdsc.flags  = DSDESC_WIDTH | DSDESC_HEIGHT;
     sdsc.width  = dsc->width;
     sdsc.height = dsc->height;

     if (pixelformat) {
          sdsc.flags       |= DSDESC_PIXELFORMAT;
          sdsc.pixelformat  = pixelformat;
     }

     DFBCHECK(dfb->CreateSurface( dfb, &sdsc, &frame ));
}

static bool frame_blitter( DirectHash *stack, unsigned long id, void *value, void *ctx )
{
     struct stack_entry    *entry = value;
     IDirectFBSurface      *dst   = entry->surface;
     DFBSurfaceDescription *dsc   = ctx;

     dst->SetBlittingFlags( dst, DSBLIT_NOFX );

     if (width != dsc->width || height != dsc->height)
          dst->StretchBlit( dst, frame, NULL, NULL );
     else
          dst->Blit( dst, frame, NULL, 0, 0 );

     if (logo) {
          dst->SetColor( dst, 0x22, 0x33, 0xbb, 0xff );
          dst->SetBlittingFlags( dst, DSBLIT_COLORIZE | DSBLIT_BLEND_ALPHACHANNEL );
          dst->Blit( dst, logo, NULL, 5, 5 );
     }

     dst->Flip( dst, NULL, DSFLIP_NONE );

     return true;
}

/******************************************************************************/

static void print_usage()
{
     printf( "DirectFB Image Sample Viewer\n\n" );
     printf( "Usage: df_image_sample [options] image\n\n" );
     printf( "Options:\n\n" );
     printf( "  --info                   Dump image info.\n" );
     printf( "  --no-logo                Do not display DirectFB logo in the upper-left corner of the window.\n" );
     printf( "  --format=<pixelformat>   Select the pixelformat to use.\n" );
     printf( "  --size=<width>x<height>  Set windows size.\n" );
     printf( "  --windows=<N>            Display image on N windows (default:1, maximum:20).\n" );
     printf( "  --help                   Print usage information.\n" );
     printf( "  --dfb-help               Output DirectFB usage information.\n\n" );
     printf( "Use:\n" );
     printf( "  ESC,Q,q to quit\n" );
}

static void dfb_shutdown()
{
     if (window_stack) destroy_stack();
     if (logo)         logo->Release( logo );
     if (frame)        frame->Release( frame );
     if (image)        image->Release( image );
     if (event_buffer) event_buffer->Release( event_buffer );
     if (layer)        layer->Release( layer );
     if (dfb)          dfb->Release( dfb );
}

int main( int argc, char *argv[] )
{
     DFBSurfaceDescription  dsc;
     int                    i;
     const char            *mrl = NULL;

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
               if (!strncmp( option, "-format=", sizeof("-format=") - 1 )) {
                    option += sizeof("-format=") - 1;
                    pixelformat = dfb_pixelformat_parse( option );
               } else
               if (!strncmp( option, "-size=", sizeof("-size=") - 1 )) {
                    option += sizeof("-size=") - 1;
                    sscanf( option, "%dx%d", &width, &height );
               } else
               if (!strncmp( option, "-windows=", sizeof("-windows=") - 1 )) {
                    option += sizeof("-windows=") - 1;
                    n_windows = strtol( option, NULL, 10 );
                    n_windows = CLAMP( n_windows, 1, 20 );
               }
          }
          else if (i == argc - 1) {
               mrl = option;
          }
     }

     if (!mrl || !*mrl) {
          print_usage();
          return 1;
     }

     /* create the main interface */
     DFBCHECK(DirectFBCreate( &dfb ));

     /* get the primary display layer */
     DFBCHECK(dfb->GetDisplayLayer( dfb, DLID_PRIMARY, &layer ));

     /* create an event buffer */
     DFBCHECK(dfb->CreateEventBuffer( dfb, &event_buffer ));

     /* create an image provider */
     DFBCHECK(dfb->CreateImageProvider( dfb, mrl, &image ));

     /* retrieve a surface description of the image */
     image->GetSurfaceDescription( image, &dsc );

     /* dump image information */
     if (info)
          dump_image_info( &dsc );

     /* create surface for the image */
     create_frame( &dsc );

     /* create logo */
     if (use_logo)
          DFBCHECK(dfb->CreateSurface( dfb, &dfblogo_desc, &logo ));

     /* create window stack */
     create_stack( &dsc );

     /* render the image */
     image->RenderTo( image, frame, NULL );

     /* recursively blit frame to windows */
     direct_hash_iterate( window_stack, frame_blitter, &dsc );

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
                                   dfb_shutdown();
                                   return 42;

                              default:
                                   break;
                         }
                         break;

                    case DWET_CLOSE: {
                         IDirectFBWindow *window;
                         window = remove_window( evt.window_id );
                         if (window) {
                              if (--n_windows <= 0) {
                                   dfb_shutdown();
                                   return 42;
                              }
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

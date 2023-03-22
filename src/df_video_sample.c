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
#include <direct/thread.h>
#include <direct/util.h>
#include <directfb.h>
#include <directfb_strings.h>

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
static IDirectFBVideoProvider *video        = NULL;
static IDirectFBSurface       *frame        = NULL;
static IDirectFBSurface       *logo         = NULL;

/* window struct */
struct stack_entry {
     IDirectFBWindow  *window;
     IDirectFBSurface *surface;
};

/* window hash table */
static DirectHash  *window_stack = NULL;
static DirectMutex  window_mutex = DIRECT_MUTEX_INITIALIZER();

/* command line options */
static int                   info        = 0;
static int                   use_logo    = 1;
static DFBSurfacePixelFormat pixelformat = DSPF_UNKNOWN;
static int                   width       = 0;
static int                   height      = 0;
static int                   n_windows   = 1;

/* progressive logo */
static DFBRectangle logo_rect[2];
static DFBColor     logo_color    = { 0x22, 0x33, 0xbb, 0xff };
static int          logo_progress = 0;

/******************************************************************************/

static const DirectFBPixelFormatNames(format_names)

static DFBSurfacePixelFormat parse_pixelformat( const char *format )
{
     int i;

     for (i = 0; i < D_ARRAY_SIZE(format_names); i++) {
          if (!strcmp( format, format_names[i].name ))
               return format_names[i].format;
     }

     return DSPF_UNKNOWN;
}

static void dump_stream_info( DFBSurfaceDescription *dsc )
{
     DFBStreamDescription desc;

     video->GetStreamDescription( video, &desc );

     printf( "  # Video: %s, %dx%d (ratio %.3f), %.2f fps, %d Kbits/s\n",
             *desc.video.encoding ? desc.video.encoding : "Unknown",
             dsc->width, dsc->height, desc.video.aspect,
             desc.video.framerate, desc.video.bitrate / 1000 );

     if (desc.caps & DVSCAPS_AUDIO) {
          printf( "  # Audio: %s, %d Khz, %d channel(s), %d Kbits/s\n",
                  *desc.audio.encoding ? desc.audio.encoding : "Unknown",
                  desc.audio.samplerate / 1000, desc.audio.channels,
                  desc.audio.bitrate / 1000 );
     }
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

     /* draw progressive logo */
     if (logo) {
          /* elapsed */
          dst->SetColor( dst, logo_color.r, logo_color.g, logo_color.b, 0xff );
          dst->SetBlittingFlags( dst, DSBLIT_COLORIZE | DSBLIT_BLEND_ALPHACHANNEL );
          dst->Blit( dst, logo, &logo_rect[0], 7, height - dfblogo_desc.height - 7 );
          /* remaining */
          dst->SetBlittingFlags( dst, DSBLIT_BLEND_ALPHACHANNEL );
          dst->Blit( dst, logo, &logo_rect[1], 7 + logo_rect[0].w, height - dfblogo_desc.height - 7 );
     }

     dst->Flip( dst, NULL, DSFLIP_NONE );

     return true;
}

static void frame_cb( void *ctx )
{
     DFBSurfaceDescription *dsc = ctx;

     /* setup coordinates for progressive logo */
     if (logo) {
          logo_rect[0].y = logo_rect[1].y = 0;
          logo_rect[0].h = logo_rect[1].h = dfblogo_desc.height;
          /* elapsed part */
          logo_rect[0].x = 0;
          logo_rect[0].w = dfblogo_desc.width * logo_progress / 100;
          /* remainig part */
          logo_rect[1].x = logo_rect[0].w;
          logo_rect[1].w = dfblogo_desc.width - logo_rect[0].w;
     }

     /* recursively blit frame to windows */
     direct_mutex_lock( &window_mutex );
     direct_hash_iterate( window_stack, frame_blitter, dsc );
     direct_mutex_unlock( &window_mutex );

     /* rotate colors */
     if (logo) {
          logo_color.r -= 2;
          logo_color.g += 1;
          logo_color.b -= 2;
     }
}

/******************************************************************************/

static void seek( double step )
{
     double pos = 0.0;

     video->GetPos( video, &pos );

     pos += step;
     if (pos < 0)
          pos = 0;

     video->SeekTo( video, pos );
}

static void set_speed( double step )
{
     double speed = 1.0;

     video->GetSpeed( video, &speed );

     if (speed == 0.0 && step > 1.0)
          speed = 0.1;

     speed *= step;

     video->SetSpeed( video, speed );
}

static void adjust_color( DFBColorAdjustmentFlags flags, int step )
{
     DFBColorAdjustment adj;

     if (video->GetColorAdjustment( video, &adj ) != DFB_OK)
          return;

     adj.flags = flags;

     if (flags & DCAF_BRIGHTNESS)
          adj.brightness = CLAMP( adj.brightness + step, 0, 0xffff );

     if (flags & DCAF_CONTRAST)
          adj.contrast = CLAMP( adj.contrast + step, 0, 0xffff );

     if (flags & DCAF_HUE)
          adj.hue = CLAMP( adj.hue + step, 0, 0xffff );

     if (flags & DCAF_SATURATION)
          adj.saturation = CLAMP( adj.saturation + step, 0, 0xffff );

     video->SetColorAdjustment( video, &adj );
}

static void set_volume( float step )
{
     float volume = 0.0f;

     video->GetVolume( video, &volume );

     video->SetVolume( video, volume + step );
}

/******************************************************************************/

static void print_usage()
{
     printf( "DirectFB Video Sample Viewer\n\n" );
     printf( "Usage: df_video_sample [options] video\n\n" );
     printf( "Options:\n\n" );
     printf( "  --info                   Dump stream info.\n" );
     printf( "  --no-logo                Do not display DirectFB logo in the lower-left corner of the window.\n" );
     printf( "  --format=<pixelformat>   Select the pixelformat to use.\n" );
     printf( "  --size=<width>x<height>  Set windows size.\n" );
     printf( "  --windows=<N>            Play video on N windows (default:1, maximum:20).\n" );
     printf( "  --help                   Print usage information.\n" );
     printf( "  --dfb-help               Output DirectFB usage information.\n\n" );
     printf( "Use:\n" );
     printf( "  ESC,Q,q     to quit\n" );
     printf( "  Enter       to stop/start playback\n" );
     printf( "  Space,P,p   to pause/resume playback\n" );
     printf( "  left,right  to seek\n" );
     printf( "  up,down     to increase/decrease playback speed\n" );
     printf( "  +,-         to increase/decrease volume level\n" );
     printf( "  B,b + right to increase brightness\n" );
     printf( "  B,b + left  to decrease brightness\n" );
     printf( "  C,c + right to increase contrast\n" );
     printf( "  C,c + left  to decrease contrast\n" );
     printf( "  S,s + right to increase saturation\n" );
     printf( "  S,s + left  to decrease saturation\n" );
     printf( "  H,h + right to increase hue\n" );
     printf( "  H,h + left  to decrease hue\n" );
}

static void dfb_shutdown()
{
     if (window_stack) destroy_stack();
     if (logo)         logo->Release( logo );
     if (frame)        frame->Release( frame );
     if (video)        video->Release( video );
     if (event_buffer) event_buffer->Release( event_buffer );
     if (layer)        layer->Release( layer );
     if (dfb)          dfb->Release( dfb );
}

int main( int argc, char *argv[] )
{
     DFBVideoProviderCapabilities  caps;
     DFBSurfaceDescription         dsc;
     int                           i;
     DFBColorAdjustmentFlags       flags = DCAF_NONE;
     const char                   *mrl   = NULL;

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
                    pixelformat = parse_pixelformat( option );
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

     i = 0;

     /* create the main interface */
     DFBCHECK(DirectFBCreate( &dfb ));

     /* get the primary display layer */
     DFBCHECK(dfb->GetDisplayLayer( dfb, DLID_PRIMARY, &layer ));

     /* create an event buffer */
     DFBCHECK(dfb->CreateEventBuffer( dfb, &event_buffer ));

     /* create a video provider */
     DFBCHECK(dfb->CreateVideoProvider( dfb, mrl, &video ));

     /* retrieve video provider capabilities */
     video->GetCapabilities( video, &caps );

     /* retrieve a surface description of the video */
     video->GetSurfaceDescription( video, &dsc );

     /* dump stream information */
     if (info)
          dump_stream_info( &dsc );

     /* create surface for video frames */
     create_frame( &dsc );

     /* create logo */
     if (use_logo)
          DFBCHECK(dfb->CreateSurface( dfb, &dfblogo_desc, &logo ));

     /* create window stack */
     create_stack( &dsc );

     /* enable gapless looping playback */
     video->SetPlaybackFlags( video, DVPLAY_LOOPING );

     /* start playback */
     video->PlayTo( video, frame, NULL, frame_cb, &dsc );

     /* main loop */
     while (1) {
          DFBWindowEvent         evt;
          DFBVideoProviderStatus status = DVSTATE_UNKNOWN;

          if (logo) {
               if (event_buffer->WaitForEventWithTimeout( event_buffer, 0, 150 ) == DFB_TIMEOUT) {
                    double len = 0.0;
                    double pos = 0.0;

                    video->GetLength( video, &len );
                    video->GetPos( video, &pos );

                    if (len > 0.0 && pos > 0.0)
                         logo_progress = pos * 100.0 / len + 0.5;
                    else
                         logo_progress = 0;

                    continue;
               }
          }

          /* process event buffer */
          while (event_buffer->GetEvent( event_buffer, DFB_EVENT(&evt) ) == DFB_OK) {
               switch (evt.type) {
                    case DWET_KEYDOWN:
                         if ((caps & DVCAPS_INTERACTIVE) &&
                             (evt.modifiers & DIMM_META) &&
                             DFB_LOWER_CASE( evt.key_symbol ) == DIKS_SMALL_I)
                              i = !i;

                         if (i) {
                              video->SendEvent( video, DFB_EVENT(&evt) );
                              break;
                         }

                         switch (DFB_LOWER_CASE( evt.key_symbol )) {
                              case DIKS_ESCAPE:
                              case DIKS_SMALL_Q:
                              case DIKS_BACK:
                              case DIKS_STOP:
                              case DIKS_EXIT:
                                   dfb_shutdown();
                                   return 42;

                              case DIKS_SPACE:
                              case DIKS_SMALL_P:
                                   if (caps & DVCAPS_SPEED) {
                                        double speed;
                                        video->GetSpeed( video, &speed );
                                        speed = (speed != 0.0) ? 0.0 : 1.0;
                                        video->SetSpeed( video, speed );
                                        break;
                                   }

                              case DIKS_ENTER:
                                   video->GetStatus( video, &status );
                                   if (status != DVSTATE_PLAY)
                                        video->PlayTo( video, frame, NULL, frame_cb, &dsc );
                                   else
                                        video->Stop( video );
                                   break;

                              case DIKS_SMALL_B:
                                   if (caps & DVCAPS_BRIGHTNESS)
                                        flags |= DCAF_BRIGHTNESS;
                                   break;
                              case DIKS_SMALL_C:
                                   if (caps & DVCAPS_CONTRAST)
                                        flags |= DCAF_CONTRAST;
                                   break;
                              case DIKS_SMALL_H:
                                   if (caps & DVCAPS_HUE)
                                        flags |= DCAF_HUE;
                                   break;
                              case DIKS_SMALL_S:
                                   if (caps & DVCAPS_SATURATION)
                                        flags |= DCAF_SATURATION;
                                   break;

                              case DIKS_CURSOR_LEFT:
                                   if (flags)
                                        adjust_color( flags, -257 );
                                   else if (caps & DVCAPS_SEEK)
                                        seek( -10.0 );
                                   break;

                              case DIKS_CURSOR_RIGHT:
                                   if (flags)
                                        adjust_color( flags, 257 );
                                   else if (caps & DVCAPS_SEEK)
                                        seek( 10.0 );
                                   break;

                              case DIKS_CURSOR_UP:
                                   if (caps & DVCAPS_SPEED)
                                        set_speed( 2.0 );
                                   break;

                              case DIKS_CURSOR_DOWN:
                                   if (caps & DVCAPS_SPEED)
                                        set_speed( 0.5 );
                                   break;

                              case DIKS_PLUS_SIGN:
                                   if (caps & DVCAPS_VOLUME)
                                        set_volume( 0.1 );
                                   break;

                              case DIKS_MINUS_SIGN:
                                   if (caps & DVCAPS_VOLUME)
                                        set_volume( -0.1 );
                                   break;

                              default:
                                   break;
                         }
                         break;

                    case DWET_KEYUP:
                         if (i) {
                              video->SendEvent( video, DFB_EVENT(&evt) );
                              break;
                         }

                         switch (DFB_LOWER_CASE( evt.key_symbol )) {
                              case DIKS_SMALL_B:
                                   if (caps & DVCAPS_BRIGHTNESS)
                                        flags &= ~DCAF_BRIGHTNESS;
                                   break;
                              case DIKS_SMALL_C:
                                   if (caps & DVCAPS_CONTRAST)
                                        flags &= ~DCAF_CONTRAST;
                                   break;
                              case DIKS_SMALL_H:
                                   if (caps & DVCAPS_HUE)
                                        flags &= ~DCAF_HUE;
                                   break;
                              case DIKS_SMALL_S:
                                   if (caps & DVCAPS_SATURATION)
                                        flags &= ~DCAF_SATURATION;
                                   break;
                              default:
                                   break;
                         }
                         break;

                    case DWET_BUTTONDOWN:
                    case DWET_BUTTONUP:
                    case DWET_ENTER:
                    case DWET_MOTION:
                    case DWET_LEAVE:
                         if (i) {
                              /* scale window coordinates to video coordinates */
                              evt.x = evt.x * dsc.width  / width;
                              evt.y = evt.y * dsc.height / height;

                              video->SendEvent( video, DFB_EVENT(&evt) );
                              break;
                         }
                         break;

                    case DWET_CLOSE: {
                         IDirectFBWindow *window;
                         direct_mutex_lock( &window_mutex );
                         window = remove_window( evt.window_id );
                         if (window) {
                              if (--n_windows <= 0) {
                                   direct_mutex_unlock( &window_mutex );
                                   dfb_shutdown();
                                   return 42;
                              }
                         }
                         direct_mutex_unlock( &window_mutex );
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

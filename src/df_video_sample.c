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
static IDirectFB             *dfb          = NULL;
static IDirectFBDisplayLayer *layer        = NULL;
static IDirectFBEventBuffer  *event_buffer = NULL;
static IDirectFBSurface      *logo         = NULL;

/* window struct */
struct stack_entry {
     IDirectFBWindow        *window;
     IDirectFBSurface       *surface;
     IDirectFBVideoProvider *video_provider;
     int                     progress;
};

/* window hash table */
static DirectHash *window_stack = NULL;

/* list of video files */
static char **mrl_list;
static int    mrl_count;

/* command line options */
static int info       = 0;
static int use_logo   = 1;
static int win_width  = 0;
static int win_height = 0;

/* logo color */
static DFBColor logo_color = { 0x22, 0x33, 0xbb, 0xff };

/**********************************************************************************************************************/

static bool logo_progress( DirectHash *stack, unsigned long id, void *value, void *ctx )
{
     struct stack_entry     *entry          = value;
     IDirectFBVideoProvider *video_provider = entry->video_provider;

     double len = 0.0;
     double pos = 0.0;

     video_provider->GetLength( video_provider, &len );
     video_provider->GetPos( video_provider, &pos );

     if (len > 0.0 && pos > 0.0)
          entry->progress = pos * 100.0 / len + 0.5;
     else
          entry->progress = 0;

     return true;
}

static void frame_cb( void *ctx )
{
     struct stack_entry *entry    = ctx;
     IDirectFBSurface   *surface  = entry->surface;
     int                 progress = entry->progress;

     /* draw progressive logo */
     if (logo) {
          int          width, height;
          DFBRectangle rect[2];

          surface->GetSize( surface, &width, &height );

          /* setup coordinates */
          if (logo) {
               rect[0].y = rect[1].y = 0;
               rect[0].h = rect[1].h = tinylogo_desc.height;
               /* elapsed part */
               rect[0].x = 0;
               rect[0].w = tinylogo_desc.width * progress / 100;
               /* remainig part */
               rect[1].x = rect[0].w;
               rect[1].w = tinylogo_desc.width - rect[0].w;
          }

          /* elapsed */
          surface->SetColor( surface, logo_color.r, logo_color.g, logo_color.b, 0xff );
          surface->SetBlittingFlags( surface, DSBLIT_COLORIZE | DSBLIT_BLEND_ALPHACHANNEL );
          surface->Blit( surface, logo, &rect[0], 7, height - tinylogo_desc.height - 7 );
          /* remaining */
          surface->SetBlittingFlags( surface, DSBLIT_BLEND_ALPHACHANNEL );
          surface->Blit( surface, logo, &rect[1], 7 + rect[0].w, height - tinylogo_desc.height - 7 );
     }

     surface->Flip( surface, NULL, DSFLIP_NONE );

     /* rotate colors */
     if (logo) {
          logo_color.r -= 2;
          logo_color.g += 1;
          logo_color.b -= 2;
     }
}

/**********************************************************************************************************************/

static void add_window( IDirectFBVideoProvider *video_provider, DFBSurfaceDescription *sdsc )
{
     DFBWindowID           id;
     DFBWindowDescription  wdsc;
     struct stack_entry   *entry;
     IDirectFBWindow      *window;
     IDirectFBSurface     *surface;

     wdsc.flags  = DWDESC_POSX | DWDESC_POSY | DWDESC_WIDTH | DWDESC_HEIGHT;
     wdsc.posx   = 32 * direct_hash_count( window_stack );
     wdsc.posy   = 18 * direct_hash_count( window_stack );
     wdsc.width  = win_width  ?: sdsc->width;
     wdsc.height = win_height ?: sdsc->height;

     entry = D_CALLOC( 1, sizeof(struct stack_entry) );

     DFBCHECK(layer->CreateWindow( layer, &wdsc, &window ));
     DFBCHECK(window->GetSurface( window, &surface ));
     DFBCHECK(window->AttachEventBuffer( window, event_buffer ));

     surface->Clear( surface, 0x00, 0x00, 0x00, 0xff );
     surface->Flip( surface, NULL, DSFLIP_NONE );

     window->GetID( window, &id );
     window->SetOpacity( window, 0xff );
     window->RequestFocus( window );

     entry->window         = window;
     entry->surface        = surface;
     entry->video_provider = video_provider;

     direct_hash_insert( window_stack, id, entry );

     /* enable gapless looping playback */
     video_provider->SetPlaybackFlags( video_provider, DVPLAY_LOOPING );

     /* start video playback */
     video_provider->PlayTo( video_provider, surface, NULL, frame_cb, entry );
}

static bool remove_window( DFBWindowID id )
{
     struct stack_entry *entry = direct_hash_lookup( window_stack, id );

     if (entry) {
          entry->video_provider->Release( entry->video_provider );
          entry->surface->Release( entry->surface );
          entry->window->Release( entry->window );
          D_FREE( entry );
          direct_hash_remove( window_stack, id );
          return true;
     }
     else
          return false;
}

static bool stack_destructor( DirectHash *stack, unsigned long id, void *value, void *ctx )
{
     struct stack_entry *entry = value;

     entry->video_provider->Release( entry->video_provider );
     entry->surface->Release( entry->surface );
     entry->window->Release( entry->window );
     D_FREE( entry );

     return true;
}

static void destroy_stack()
{
     direct_hash_iterate( window_stack, stack_destructor, NULL );

     /* destroy hash table */
     direct_hash_destroy( window_stack );
}

/**********************************************************************************************************************/

static void adjust_color( DFBWindowID id, DFBColorAdjustmentFlags flags, int step )
{
     DFBColorAdjustment      adj;
     struct stack_entry     *entry;
     IDirectFBVideoProvider *video_provider;

     entry = direct_hash_lookup( window_stack, id );
     if (!entry)
          return;

     video_provider = entry->video_provider;

     if (video_provider->GetColorAdjustment( video_provider, &adj ) != DFB_OK)
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

     video_provider->SetColorAdjustment( video_provider, &adj );
}

static void pause_resume( DFBWindowID id )
{
     double                  speed;
     struct stack_entry     *entry;
     IDirectFBVideoProvider *video_provider;

     entry = direct_hash_lookup( window_stack, id );
     if (!entry)
          return;

     video_provider = entry->video_provider;

     if (video_provider->GetSpeed( video_provider, &speed ) != DFB_OK)
          return;

     speed = (speed != 0.0) ? 0.0 : 1.0;

     video_provider->SetSpeed( video_provider, speed );
}

static void stop_start( DFBWindowID id )
{
     DFBVideoProviderStatus  status;
     struct stack_entry     *entry;
     IDirectFBSurface       *surface;
     IDirectFBVideoProvider *video_provider;

     entry = direct_hash_lookup( window_stack, id );
     if (!entry)
          return;

     surface        = entry->surface;
     video_provider = entry->video_provider;

     if (video_provider->GetStatus( video_provider, &status ) != DFB_OK)
          return;

     if (status != DVSTATE_PLAY)
          video_provider->PlayTo( video_provider, surface, NULL, frame_cb, entry );
     else
          video_provider->Stop( video_provider );
}

static void seek( DFBWindowID id, double step )
{
     double                  pos;
     struct stack_entry     *entry;
     IDirectFBVideoProvider *video_provider;

     entry = direct_hash_lookup( window_stack, id );
     if (!entry)
          return;

     video_provider = entry->video_provider;

     if (video_provider->GetPos( video_provider, &pos ) != DFB_OK)
          return;

     pos += step;
     if (pos < 0)
          pos = 0;

     video_provider->SeekTo( video_provider, pos );
}

static void send_input_event( DFBWindowID id, DFBWindowEvent *evt )
{
     struct stack_entry     *entry;
     IDirectFBVideoProvider *video_provider;

     entry = direct_hash_lookup( window_stack, id );
     if (!entry)
          return;

     video_provider = entry->video_provider;

     video_provider->SendEvent( video_provider, DFB_EVENT(evt) );
}

static void set_speed( DFBWindowID id, double step )
{
     double                  speed;
     struct stack_entry     *entry;
     IDirectFBVideoProvider *video_provider;

     entry = direct_hash_lookup( window_stack, id );
     if (!entry)
          return;

     video_provider = entry->video_provider;

     if (video_provider->GetSpeed( video_provider, &speed ) != DFB_OK)
          return;

     if (speed == 0.0 && step > 1.0)
          speed = 0.1;

     speed *= step;

     video_provider->SetSpeed( video_provider, speed );
}

static void set_volume( DFBWindowID id, float step )
{
     float                   volume;
     struct stack_entry     *entry;
     IDirectFBVideoProvider *video_provider;

     entry = direct_hash_lookup( window_stack, id );
     if (!entry)
          return;

     video_provider = entry->video_provider;

     if (video_provider->GetVolume( video_provider, &volume ) != DFB_OK)
          return;

     video_provider->SetVolume( video_provider, volume + step );
}

/**********************************************************************************************************************/

static void dfb_shutdown()
{
     if (window_stack) destroy_stack();
     if (logo)         logo->Release( logo );
     if (event_buffer) event_buffer->Release( event_buffer );
     if (layer)        layer->Release( layer );
     if (dfb)          dfb->Release( dfb );
}

static void print_usage()
{
     printf( "DirectFB Video Sample Viewer\n\n" );
     printf( "Usage: df_video_sample [options] files\n\n" );
     printf( "Options:\n\n" );
     printf( "  --info                   Dump stream info.\n" );
     printf( "  --no-logo                Do not display DirectFB logo in the lower-left corner of the window.\n" );
     printf( "  --size=<width>x<height>  Set windows size.\n" );
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

int main( int argc, char *argv[] )
{
     int                          i;
     DFBVideoProviderCapabilities caps;
     DFBColorAdjustmentFlags      flags = DCAF_NONE;

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
          IDirectFBVideoProvider *video_provider;

          /* create a video provider */
          DFBCHECK(dfb->CreateVideoProvider( dfb, mrl_list[i], &video_provider ));

          /* retrieve video provider capabilities */
          video_provider->GetCapabilities( video_provider, &caps );

          /* retrieve a surface description of the video */
          video_provider->GetSurfaceDescription( video_provider, &sdsc );

          /* dump stream information */
          if (info) {
               DFBStreamDescription desc;

               video_provider->GetStreamDescription( video_provider, &desc );

               printf( "%s\n", mrl_list[i] );
               printf( "  # Video: %s, %dx%d (ratio %.3f), %.2f fps, %d Kbits/s\n",
                       *desc.video.encoding ? desc.video.encoding : "Unknown",
                       sdsc.width, sdsc.height, desc.video.aspect,
                       desc.video.framerate, desc.video.bitrate / 1000 );

               if (desc.caps & DVSCAPS_AUDIO)
                    printf( "  # Audio: %s, %d Khz, %d channel(s), %d Kbits/s\n",
                            *desc.audio.encoding ? desc.audio.encoding : "Unknown",
                            desc.audio.samplerate / 1000, desc.audio.channels,
                            desc.audio.bitrate / 1000 );
          }

          /* add window to the stack */
          add_window( video_provider, &sdsc );
     }

     /* video provider input interactivity */
     i = 0;

     /* main loop */
     while (1) {
          DFBWindowEvent evt;

          /* recursively update windows logo progress */
          if (logo) {
               if (event_buffer->WaitForEventWithTimeout( event_buffer, 0, 150 ) == DFB_TIMEOUT) {
                    direct_hash_iterate( window_stack, logo_progress, NULL );
                    continue;
               }
          }
          else
               event_buffer->WaitForEvent( event_buffer );

          /* process event buffer */
          while (event_buffer->GetEvent( event_buffer, DFB_EVENT(&evt) ) == DFB_OK) {
               switch (evt.type) {
                    case DWET_KEYDOWN:
                         if ((caps & DVCAPS_INTERACTIVE) &&
                             (evt.modifiers & DIMM_META) &&
                             DFB_LOWER_CASE( evt.key_symbol ) == DIKS_SMALL_I)
                              i = !i;

                         if (i) {
                              send_input_event( evt.window_id, &evt );
                              break;
                         }

                         switch (DFB_LOWER_CASE( evt.key_symbol )) {
                              case DIKS_ESCAPE:
                              case DIKS_SMALL_Q:
                              case DIKS_BACK:
                              case DIKS_STOP:
                              case DIKS_EXIT:
                                   return 42;

                              case DIKS_SPACE:
                              case DIKS_SMALL_P:
                                   pause_resume( evt.window_id );
                                   break;

                              case DIKS_ENTER:
                                   stop_start( evt.window_id );
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
                                        adjust_color( evt.window_id, flags, -257 );
                                   else
                                        seek( evt.window_id, -10.0 );
                                   break;

                              case DIKS_CURSOR_RIGHT:
                                   if (flags)
                                        adjust_color( evt.window_id, flags, 257 );
                                   else
                                        seek( evt.window_id, 10.0 );
                                   break;

                              case DIKS_CURSOR_UP:
                                   set_speed( evt.window_id, 2.0 );
                                   break;

                              case DIKS_CURSOR_DOWN:
                                   set_speed( evt.window_id, 0.5 );
                                   break;

                              case DIKS_PLUS_SIGN:
                                   set_volume( evt.window_id, 0.1 );
                                   break;

                              case DIKS_MINUS_SIGN:
                                   set_volume( evt.window_id, -0.1 );
                                   break;

                              default:
                                   break;
                         }
                         break;

                    case DWET_KEYUP:
                         if (i) {
                              send_input_event( evt.window_id, &evt );
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
                    case DWET_MOTION:
                    case DWET_ENTER:
                    case DWET_LEAVE:
                         if (i) {
                              send_input_event( evt.window_id, &evt );
                              break;
                         }
                         break;

                    case DWET_CLOSE: {
                         if (remove_window( evt.window_id )) {
                              if (--mrl_count <= 0) {
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

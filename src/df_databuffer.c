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

#include <direct/filesystem.h>
#include <direct/thread.h>
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
static IDirectFB              *dfb            = NULL;
static IDirectFBSurface       *primary        = NULL;
static IDirectFBSurface       *surface        = NULL;
static IDirectFBFont          *font           = NULL;
static IDirectFBImageProvider *image_provider = NULL;
static IDirectFBVideoProvider *video_provider = NULL;

/* screen width and height */
static int screen_width, screen_height;

/* font description */
static DFBFontDescription fdsc;

/* filenames in command line */
static const char *fontfile  = NULL;
static const char *mediafile = NULL;

/* command line options */
static int use_image = 0;
static int use_video = 0;

/**********************************************************************************************************************/

static DIRenderCallbackResult render_callback( DFBRectangle *rect, void *ctx )
{
     int width, height;

     surface->GetSize( surface, &width, &height );

     primary->Blit( primary, surface, rect, (screen_width - width) / 2, (screen_height - height) / 2 + rect->y );

     return DIRCR_OK;
}

static void frame_callback( void *ctx )
{
     int width, height;

     surface->GetSize( surface, &width, &height );

     primary->Blit( primary, surface, NULL, (screen_width - width) / 2, (screen_height - height) / 2 );
}

static void test_file()
{
     DFBResult                 ret = DFB_FAILURE;
     DFBDataBufferDescription  ddsc;
     DFBSurfaceDescription     sdsc;
     IDirectFBDataBuffer      *buffer;

     primary->Clear( primary, 0, 0, 0, 0 );

     /* use file data buffer */
     ddsc.flags = DBDESC_FILE;

     /* load font */
     ddsc.file = fontfile;
     DFBCHECK(dfb->CreateDataBuffer( dfb, &ddsc, &buffer ));
     DFBCHECK(buffer->CreateFont( buffer, &fdsc, &font ));
     DFBCHECK(primary->SetFont( primary, font ));
     DFBCHECK(primary->DrawString( primary, "File data buffer", -1, 10, 10, DSTF_TOPLEFT ));

     /* load media */
     ddsc.file = mediafile;
     DFBCHECK(dfb->CreateDataBuffer( dfb, &ddsc, &buffer ));
     if (!use_video) {
          ret = buffer->CreateImageProvider( buffer, &image_provider );
     }
     if (ret == DFB_OK) {
          DFBCHECK(image_provider->GetSurfaceDescription( image_provider, &sdsc ));
     }
     else if (!use_image) {
          ret = buffer->CreateVideoProvider( buffer, &video_provider );
          if (ret == DFB_OK) {
               DFBCHECK(video_provider->GetSurfaceDescription( video_provider, &sdsc ));
               DFBCHECK(primary->GetPixelFormat( primary, &sdsc.pixelformat ));
               DFBCHECK(primary->GetColorSpace( primary, &sdsc.colorspace ));
          }
     }

     if (ret)
          DirectFBErrorFatal( "Couldn't load media from file data buffer!", ret );

     DFBCHECK(dfb->CreateSurface( dfb, &sdsc, &surface ));

     if (image_provider) {
          image_provider->SetRenderCallback( image_provider, render_callback, NULL );

          DFBCHECK(image_provider->RenderTo( image_provider, surface, NULL ));
     }
     else { /* video_provider */
          DFBCHECK(video_provider->PlayTo( video_provider, surface, NULL, frame_callback, NULL ));

          sleep( 3 );

          video_provider->Stop( video_provider );
     }

     surface->Release( surface );
     surface = NULL;

     if (image_provider) {
          image_provider->Release( image_provider );
          image_provider = NULL;
     }

     if (video_provider) {
          video_provider->Release( video_provider );
          video_provider = NULL;
     }

     font->Release( font );
     font = NULL;
}

static void test_memory()
{
     DFBResult                 ret = DFB_FAILURE;
     DirectFile                fd;
     DirectFileInfo            info;
     DFBDataBufferDescription  ddsc;
     DFBSurfaceDescription     sdsc;
     unsigned int              fontlength;
     unsigned int              medialength;
     void                     *fontdata;
     void                     *mediadata;
     IDirectFBDataBuffer      *buffer;

     primary->Clear( primary, 0, 0, 0, 0 );

     /* use memory data buffer */
     ddsc.flags = DBDESC_MEMORY;

     /* memory map and size of the font file */
     direct_file_open( &fd, fontfile, O_RDONLY, 0 );
     direct_file_get_info( &fd, &info );
     fontlength = info.size;
     direct_file_map( &fd, NULL, 0, fontlength, DFP_READ, &fontdata );
     direct_file_close( &fd );

     /* memory map and size of the media file */
     direct_file_open( &fd, mediafile, O_RDONLY, 0 );
     direct_file_get_info( &fd, &info );
     medialength = info.size;
     direct_file_map( &fd, NULL, 0, medialength, DFP_READ, &mediadata );
     direct_file_close( &fd );

     /* load font */
     ddsc.memory.data   = fontdata;
     ddsc.memory.length = fontlength;
     DFBCHECK(dfb->CreateDataBuffer( dfb, &ddsc, &buffer ));
     DFBCHECK(buffer->CreateFont( buffer, &fdsc, &font ));
     DFBCHECK(primary->SetFont( primary, font ));
     DFBCHECK(primary->DrawString( primary, "Memory data buffer", -1, 10, 10, DSTF_TOPLEFT ));

     /* load media */
     ddsc.memory.data   = mediadata;
     ddsc.memory.length = medialength;
     DFBCHECK(dfb->CreateDataBuffer( dfb, &ddsc, &buffer ));
     if (!use_video) {
          ret = buffer->CreateImageProvider( buffer, &image_provider );
     }
     if (ret == DFB_OK) {
          DFBCHECK(image_provider->GetSurfaceDescription( image_provider, &sdsc ));
     }
     else if (!use_image) {
          ret = buffer->CreateVideoProvider( buffer, &video_provider );
          if (ret == DFB_OK) {
               DFBCHECK(video_provider->GetSurfaceDescription( video_provider, &sdsc ));
               DFBCHECK(primary->GetPixelFormat( primary, &sdsc.pixelformat ));
               DFBCHECK(primary->GetColorSpace( primary, &sdsc.colorspace ));
          }
     }

     if (ret)
          DirectFBErrorFatal( "Couldn't load media from memory data buffer!", ret );

     DFBCHECK(dfb->CreateSurface( dfb, &sdsc, &surface ));

     if (image_provider) {
          image_provider->SetRenderCallback( image_provider, render_callback, NULL );

          DFBCHECK(image_provider->RenderTo( image_provider, surface, NULL ));
     }
     else { /* video_provider */
          DFBCHECK(video_provider->PlayTo( video_provider, surface, NULL, frame_callback, NULL ));

          sleep( 3 );

          video_provider->Stop( video_provider );
     }

     surface->Release( surface );
     surface = NULL;

     if (image_provider) {
          image_provider->Release( image_provider );
          image_provider = NULL;
     }

     if (video_provider) {
          video_provider->Release( video_provider );
          video_provider = NULL;
     }

     font->Release( font );
     font = NULL;

     direct_file_unmap( fontdata, fontlength );
     direct_file_unmap( mediadata, medialength );
}

static void *streamer( DirectThread *thread, void *arg )
{
     DirectFile           fd;
     size_t               bytes;
     unsigned int         len;
     int                  usec;
     char                 data[8192];
     const char          *filename;
     IDirectFBDataBuffer *buffer = arg;

     if (!strcmp( direct_thread_self_name(), "Font Streamer" ))
          filename = fontfile;
     else /* Media Streamer */
          filename = mediafile;

     direct_file_open( &fd, filename, O_RDONLY, 0 );

     usec = 1000;

     while (1) {
          usleep( usec );

          DFBCHECK(buffer->GetLength( buffer, &len ));
          if (len >= 64 * 1024) {
               usec += 100;
               continue;
          }
          else {
               usec -= 100;
               usec = MAX( usec, 1000 );
          }

          direct_file_read( &fd, data, rand() % 8192 + 1, &bytes );
          if (bytes <= 0) {
               DFBCHECK(buffer->Finish( buffer ));
               break;
          }

          DFBCHECK(buffer->PutData( buffer, data, bytes ));
     }

     direct_file_close( &fd );

     return NULL;
}

static void test_streamed()
{
     DFBResult               ret = DFB_FAILURE;
     DFBSurfaceDescription   sdsc;
     DirectThread           *streaming_thread;
     IDirectFBDataBuffer    *buffer;

     primary->Clear( primary, 0, 0, 0, 0 );

     /* use streamed data buffer */

     /* load font */
     DFBCHECK(dfb->CreateDataBuffer( dfb, NULL, &buffer ));
     streaming_thread = direct_thread_create( DTT_DEFAULT, streamer, buffer, "Font Streamer" );
     DFBCHECK(buffer->CreateFont( buffer, &fdsc, &font ));
     DFBCHECK(primary->SetFont( primary, font ));
     DFBCHECK(primary->DrawString( primary, "Streamed data buffer", -1, 10, 10, DSTF_TOPLEFT ));

     /* load media */
     DFBCHECK(dfb->CreateDataBuffer( dfb, NULL, &buffer ));
     streaming_thread = direct_thread_create( DTT_DEFAULT, streamer, buffer, "Media Streamer" );
     if (!use_video) {
          ret = buffer->CreateImageProvider( buffer, &image_provider );
     }
     if (ret == DFB_OK) {
          DFBCHECK(image_provider->GetSurfaceDescription( image_provider, &sdsc ));
     }
     else if (!use_image) {
          ret = buffer->CreateVideoProvider( buffer, &video_provider );
          if (ret == DFB_OK) {
               DFBCHECK(video_provider->GetSurfaceDescription( video_provider, &sdsc ));
               DFBCHECK(primary->GetPixelFormat( primary, &sdsc.pixelformat ));
               DFBCHECK(primary->GetColorSpace( primary, &sdsc.colorspace ));
          }
     }

     if (ret)
          DirectFBErrorFatal( "Couldn't load media from streamed data buffer!", ret );

     DFBCHECK(dfb->CreateSurface( dfb, &sdsc, &surface ));

     if (image_provider) {
          image_provider->SetRenderCallback( image_provider, render_callback, NULL );

          DFBCHECK(image_provider->RenderTo( image_provider, surface, NULL ));
     }
     else { /* video_provider */
          DFBCHECK(video_provider->PlayTo( video_provider, surface, NULL, frame_callback, NULL ));

          sleep( 3 );

          video_provider->Stop( video_provider );
     }

     direct_thread_cancel( streaming_thread );
     direct_thread_join( streaming_thread );
     direct_thread_destroy( streaming_thread );

     surface->Release( surface );
     surface = NULL;

     if (image_provider) {
          image_provider->Release( image_provider );
          image_provider = NULL;
     }

     if (video_provider) {
          video_provider->Release( video_provider );
          video_provider = NULL;
     }

     font->Release( font );
     font = NULL;
}

/**********************************************************************************************************************/

static void dfb_shutdown()
{
     if (video_provider) video_provider->Release( video_provider );
     if (image_provider) image_provider->Release( image_provider );
     if (font)           font->Release( font );
     if (surface)        surface->Release( surface );
     if (primary)        primary->Release( primary );
     if (dfb)            dfb->Release( dfb );
}

static void print_usage()
{
     printf( "DirectFB DataBuffer Test\n\n" );
     printf( "Usage: df_databuffer [options] <fontfile> <imagefile>|<videofile>\n\n" );
     printf( "  --image     Use image provider.\n" );
     printf( "  --video     Use video provider.\n" );
     printf( "  --help      Print usage information.\n" );
     printf( "  --dfb-help  Output DirectFB usage information.\n\n" );
}

int main( int argc, char *argv[] )
{
     int                   i;
     DFBSurfaceDescription desc;

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
               if (!strcmp( option, "-image" )) {
                    use_image = 1;
               } else
               if (!strcmp( option, "-video" )) {
                    use_video = 1;
               }
          }
          else if (i == argc - 2){
               fontfile  = argv[i];
               mediafile = argv[i+1];
               break;
          }
     }

     if (use_image && use_video) {
          printf( "Select either an image provider or a video provider (automatic if not specified)\n" );
          return 1;
     }

     if (!fontfile || !mediafile) {
          print_usage();
          return 1;
     }

     /* create the main interface */
     DFBCHECK(DirectFBCreate( &dfb ));

     /* register termination function */
     atexit( dfb_shutdown );

     /* set the cooperative level to DFSCL_FULLSCREEN for exclusive access to the primary layer */
     dfb->SetCooperativeLevel( dfb, DFSCL_FULLSCREEN );

     /* get the primary surface, i.e. the surface of the primary layer */
     desc.flags = DSDESC_CAPS;
     desc.caps  = DSCAPS_PRIMARY;

     DFBCHECK(dfb->CreateSurface( dfb, &desc, &primary ));

     primary->GetSize( primary, &screen_width, &screen_height );

     /* set font information */
     fdsc.flags  = DFDESC_HEIGHT;
     fdsc.height = 24;

     /* set color for text */
     primary->SetColor( primary, 0xcc, 0xcc, 0xcc, 0xff );

     test_file();
     sleep( 2 );

     test_memory();
     sleep( 2 );

     test_streamed();
     sleep( 2 );

     return 0;
}

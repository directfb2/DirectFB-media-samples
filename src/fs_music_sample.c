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

#include <direct/list.h>
#include <fusionsound.h>
#include <termios.h>

/* macro for a safe call to FusionSound functions */
#define FSCHECK(x)                                                    \
     do {                                                             \
          DirectResult ret = x;                                       \
          if (ret != DR_OK) {                                         \
               fprintf( stderr, "%s <%d>:\n\t", __FILE__, __LINE__ ); \
               FusionSoundErrorFatal( #x, ret );                      \
          }                                                           \
     } while (0)

/* terminal interface */
static struct termios term;

/* FusionSound interfaces */
static IFusionSound              *sound    = NULL;
static IFusionSoundStream        *stream   = NULL;
static IFusionSoundPlayback      *playback = NULL;

/* media track struct */
typedef struct {
     DirectLink link;

     FSTrackID  id;
} MediaTrack;

/* media struct */
typedef struct {
     DirectLink  link;

     const char *mrl;
     int         id;

     DirectLink *tracks;
} Media;

/* media list */
static DirectLink *medias = NULL;

/* command line options */
static int             quiet        = 0;
static FSSampleFormat  sampleformat = FSSF_UNKNOWN;
static const char     *gain         = NULL;

/******************************************************************************/

static DirectEnumerationResult track_cb( FSTrackID track_id, FSTrackDescription desc, void *ctx )
{
     Media      *media = ctx;
     MediaTrack *track;

     track = D_CALLOC( 1, sizeof(MediaTrack) );
     if (!track) {
          D_OOM();
          return DENUM_CANCEL;
     }

     track->id = track_id;

     direct_list_append( &media->tracks, &track->link );

     return DENUM_OK;
}

/******************************************************************************/

static void print_usage()
{
     printf( "FusionSound Music Sample Player\n\n" );
     printf( "Usage: fs_music_sample [options] files\n\n" );
     printf( "Options:\n\n" );
     printf( "  --quiet              Do not print tracks and progress info.\n" );
     printf( "  --depth=<bitdepth>   Select the bitdepth to use (8, 16, 24 or 32).\n" );
     printf( "  --gain=<replaygain>  Set replay gain ('track' or 'album').\n" );
     printf( "  --help               Print usage information.\n" );
     printf( "  --fs-help            Output FusionSound usage information.\n\n" );
     printf( "Use:\n" );
     printf( "  ESC,Q,q to quit\n" );
     printf( "  s       to stop playback\n" );
     printf( "  p       to start playback\n" );
     printf( "  f       to seek forward (+15s)\n" );
     printf( "  b       to seek backward (-15s)\n" );
     printf( "  0 ... 9 to seek within the current track\n" );
     printf( "  >       to switch to next track\n" );
     printf( "  <       to switch to previous track\n" );
     printf( "  *,/     to increase/decrease playback speed\n" );
     printf( "  +,-     to increase/decrease volume level\n" );
     printf( "  l       to toggle track looping\n" );
     printf( "  r       to toggle media list repeat\n" );
}

static void fs_shutdown()
{
     Media *media, *media_next;

     if (playback) playback->Release( playback );
     if (stream)   stream->Release( stream );
     if (sound)    sound->Release( sound );

     if (isatty( STDIN_FILENO ))
          tcsetattr( STDIN_FILENO, TCSADRAIN, &term );

     direct_list_foreach_safe (media, media_next, medias) {
          MediaTrack *track, *track_next;

          direct_list_foreach_safe (track, track_next, media->tracks) {
               D_FREE( track );
          }
          D_FREE( media );
     }
}

int main( int argc, char *argv[] )
{
     int                          i;
     FSMusicProviderPlaybackFlags flags  = FMPLAY_NOFX;
     float                        volume = 1;
     float                        pitch  = 1;
     int                          dir    = 1;
     int                          repeat = 0;
     int                          quit   = 0;

     if (argc < 2) {
          print_usage();
          return 0;
     }

     /* initialize FusionSound including command line parsing */
     FSCHECK(FusionSoundInit( &argc, &argv ));

     int id = 0;

     /* parse command line */
     for (i = 1; i < argc; i++) {
          char *option = argv[i];

          if (*option == '-') {
               option++;

               if (!strcmp( option, "-help" )) {
                    print_usage();
                    return 0;
               } else
               if (!strcmp( option, "-quiet" )) {
                    quiet = 1;
               } else
               if (!strncmp( option, "-depth=", sizeof("-depth=") - 1 )) {
                    option += sizeof("-depth=") - 1;
                    switch (atoi( option )) {
                         case 8:
                              sampleformat = FSSF_U8;
                              break;
                         case 16:
                              sampleformat = FSSF_S16;
                              break;
                         case 24:
                              sampleformat = FSSF_S24;
                              break;
                         case 32:
                              sampleformat = FSSF_S32;
                              break;
                         default:
                              break;
                    }
               } else
               if (!strncmp( option, "-gain=", sizeof("-gain=") - 1 )) {
                    option += sizeof("-gain=") - 1;
                    gain = option;
               }
          }
          else {
               Media *media;

               media = D_CALLOC( 1, sizeof(Media) );
               media->mrl = option;
               media->id = id++;
               direct_list_append( &medias, &media->link );
          }
     }

     if (!medias) {
          print_usage();
          return 1;
     }

     if (isatty( STDIN_FILENO )) {
          struct termios ts;

          /* get terminal attributes. */
          tcgetattr( STDIN_FILENO, &term );

          /* set terminal attributes */
          ts = term;
          ts.c_lflag     &= ~(ICANON | ECHO);
          ts.c_cc[VTIME]  = 0;
          ts.c_cc[VMIN]   = 0;
          tcsetattr( STDIN_FILENO, TCSAFLUSH, &ts );
     }

     /* create the main interface */
     FSCHECK(FusionSoundCreate( &sound ));

     /* register termination function */
     atexit( fs_shutdown );

     do {
          Media *media, *media_next;

          for (media = dir > 0 ? (Media*) medias : direct_list_get_last( medias ); media && !quit;) {
               DirectResult               ret;
               FSTrackDescription         desc;
               FSStreamDescription        sdsc;
               IFusionSoundMusicProvider *music_provider;
               MediaTrack                *track, *track_next;
               FSMusicProviderStatus      status = FMSTATE_UNKNOWN;

               media_next = (Media*) media->link.next;

               /* create a music provider */
               ret = sound->CreateMusicProvider( sound, media->mrl, &music_provider );
               if (ret) {
                    media = media_next;
                    continue;
               }

               /* play tracks */
               FSCHECK(music_provider->EnumTracks( music_provider, track_cb, media ));

               if (!quiet)
                    fprintf( stderr, "\nMedia %d (%s):\n", media->id, media->mrl );

               for (track = dir > 0 ? (MediaTrack*) media->tracks : direct_list_get_last( media->tracks ); track && !quit;) {
                    double len       = 0;
                    int    vol_set   = 0;
                    int    pitch_set = 0;

                    track_next = (MediaTrack*) track->link.next;

                    /* select current track in playlist */
                    ret = music_provider->SelectTrack( music_provider, track->id );
                    if (ret) {
                         track = track_next;
                         continue;
                    }

                    /* get stream description */
                    music_provider->GetStreamDescription( music_provider, &sdsc );
                    if (sampleformat)
                         sdsc.sampleformat = sampleformat;

                    /* check if the stream description needs to be changed */
                    if (stream) {
                         FSStreamDescription dsc;

                         stream->GetDescription( stream, &dsc );

                         if (dsc.channels     != sdsc.channels     ||
                             dsc.sampleformat != sdsc.sampleformat ||
                             dsc.samplerate   != sdsc.samplerate) {
                              stream->Wait( stream, 0 );
                              playback->Release( playback );
                              playback = NULL;
                              stream->Release( stream );
                              stream = NULL;
                         }
                    }

                    /* create the sound stream */
                    if (!stream) {
                         ret = sound->CreateStream( sound, &sdsc, &stream );
                         if (ret) {
                              FusionSoundError( "CreateStream failed", ret );
                              break;
                         }

                         stream->GetDescription( stream, &sdsc );
                         stream->GetPlayback( stream, &playback );
                    }

                    /* get track description */
                    music_provider->GetTrackDescription( music_provider, &desc );

                    /* reset volume level */
                    if (gain) {
                         if (!strcmp( gain, "track" )) {
                              if (desc.replaygain > 0.0)
                                   volume = desc.replaygain;
                         }
                         else if (!strcmp( gain, "album" )) {
                              if (desc.replaygain_album > 0.0)
                                   volume = desc.replaygain_album;
                         }
                    }

                    playback->SetVolume( playback, volume );

                    /* reset pitch */
                    playback->SetPitch( playback, pitch );

                    /* play the selected track */
                    ret = music_provider->PlayToStream( music_provider, stream );
                    if (ret) {
                         FusionSoundError( "PlayToStream failed", ret );
                         break;
                    }

                    /* print track information */
                    if (!quiet) {
                         fprintf( stderr,
                                  "\nTrack %d.%u:\n"
                                  "  Artist:     %s\n"
                                  "  Title:      %s\n"
                                  "  Album:      %s\n"
                                  "  Year:       %d\n"
                                  "  Genre:      %s\n"
                                  "  Encoding:   %s\n"
                                  "  Bitrate:    %d Kbits/s\n"
                                  "  ReplayGain: %.2f (track), %.2f (album)\n"
                                  "  Output:     %d Hz, %d channel(s), %u bits\n\n",
                                  media->id, track->id, desc.artist, desc.title, desc.album, desc.year, desc.genre,
                                  desc.encoding, desc.bitrate / 1000, desc.replaygain, desc.replaygain_album,
                                  sdsc.samplerate, sdsc.channels, FS_BITS_PER_SAMPLE(sdsc.sampleformat) );
                    }

                    /* get track length */
                    music_provider->GetLength( music_provider, &len );

                    do {
                         double pos = 0;

                         /* get playback status */
                         music_provider->GetStatus( music_provider, &status );

                         if (!quiet) {
                              int filled = 0;
                              int total  = 0;
                              int clear  = 0;

                              /* query ring buffer status */
                              stream->GetStatus( stream, &filled, &total, NULL, NULL, NULL );

                              /* query elapsed seconds */
                              music_provider->GetPos( music_provider, &pos );

                              /* print progress information */
                              fprintf( stderr, "\rTime: %02d:%02d:%02d of %02d:%02d:%02d  Ring Buffer:%3d%% ",
                                       (int) pos / 60, (int) pos % 60, (int) (pos * 100) % 100,
                                       (int) len / 60, (int) len % 60, (int) (len * 100) % 100, filled * 100 / total );

                              if (vol_set) {
                                   if (--vol_set)
                                        fprintf( stderr, "[Vol:%3d%%] ", (int) (volume * 100) );
                                   else
                                        clear += 12;
                              }

                              if (pitch_set) {
                                   if (--pitch_set)
                                        fprintf( stderr, "[Pitch:%3d%%] ", (int) (pitch * 100) );
                                   else
                                        clear += 13;
                              }

                              while (clear) {
                                   putc( ' ', stderr );
                                   clear--;
                              }
                         }

                         if (isatty( STDIN_FILENO )) {
                              int            c;
                              fd_set         s;
                              struct timeval t = { 0, 40000 };

                              FD_ZERO( &s );
                              FD_SET( STDIN_FILENO, &s );

                              select( STDIN_FILENO + 1, &s, NULL, NULL, &t );

                              while ((c = getc( stdin )) > 0) {
                                   switch (c) {
                                        case 'p':
                                             music_provider->PlayToStream( music_provider, stream );
                                             break;
                                        case 's':
                                             if (!pitch) {
                                                  playback->SetVolume( playback, 0 );
                                                  playback->SetPitch( playback, 1 );
                                             }
                                             music_provider->Stop( music_provider );
                                             if (!pitch) {
                                                  playback->SetPitch( playback, 0 );
                                                  playback->SetVolume( playback, volume );
                                             }
                                             break;
                                        case 'f':
                                             music_provider->GetPos( music_provider, &pos );
                                             music_provider->SeekTo( music_provider, pos + 15 );
                                             break;
                                        case 'b':
                                             music_provider->GetPos( music_provider, &pos );
                                             music_provider->SeekTo( music_provider, pos - 15 );
                                             break;
                                        case '0' ... '9':
                                             if (len)
                                                  music_provider->SeekTo( music_provider, len * (c - '0') / 10 );
                                             break;
                                        case '<':
                                             if (track == (MediaTrack*) media->tracks) {
                                                  track_next = NULL;
                                                  if (media == (Media*) medias)
                                                       media_next = NULL;
                                                  else
                                                       media_next = (Media*) media->link.prev;
                                             }
                                             else
                                                  track_next = (MediaTrack*) track->link.prev;
                                        case '>':
                                             dir = c != '<' ? 1 : -1;
                                             if (!pitch) {
                                                  playback->SetVolume( playback, 0 );
                                                  playback->SetPitch( playback, 1 );
                                             }
                                             music_provider->Stop( music_provider );
                                             status = FMSTATE_FINISHED;
                                             break;
                                        case 'l':
                                             flags ^= FMPLAY_LOOPING;
                                             music_provider->SetPlaybackFlags( music_provider, flags );
                                             break;
                                        case 'r':
                                             repeat = !repeat;
                                             break;
                                        case '-':
                                             volume -= 1.0/32;
                                             if (volume < 0.0)
                                                  volume = 0.0;
                                             playback->SetVolume( playback, volume );
                                             vol_set = 50;
                                             break;
                                        case '+':
                                             volume += 1.0/32;
                                             if (volume > 64.0)
                                                  volume = 64.0;
                                             playback->SetVolume( playback, volume );
                                             vol_set = 50;
                                             break;
                                        case '/':
                                             pitch -= 1.0/32;
                                             if (pitch < 0.0)
                                                  pitch = 0.0;
                                             playback->SetPitch( playback, pitch );
                                             pitch_set = 50;
                                             break;
                                        case '*':
                                             pitch += 1.0/32;
                                             if (pitch > 64.0)
                                                  pitch = 64.0;
                                             playback->SetPitch( playback, pitch );
                                             pitch_set = 50;
                                             break;
                                        case 'q':
                                        case 'Q':
                                        case '\033':
                                             quit = 1;
                                             status = FMSTATE_FINISHED;
                                             break;
                                        default:
                                             break;
                                   }
                              }
                         }
                         else {
                              usleep( 40000 );
                         }
                    } while (status != FMSTATE_FINISHED);

                    if (!quiet)
                         fprintf( stderr, "\n" );

                    track = track_next;
               }

               /* release the music provider */
               music_provider->Release( music_provider );

               /* release media tracks */
               direct_list_foreach_safe (track, track_next, media->tracks) {
                    D_FREE( track );
               }

               media->tracks = NULL;

               media = media_next;
          }
     } while (repeat && !quit);

     return 0;
}

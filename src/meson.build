#  This file is part of DirectFB-media-samples.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
#  THE SOFTWARE.

data_inc = include_directories('../data')

executable('df_databuffer',   'df_databuffer.c',                                  dependencies: directfb_dep,    install: true)
executable('df_font_sample',  'df_font_sample.c',                                 dependencies: directfb_dep,    install: true)
executable('df_image_sample', 'df_image_sample.c', include_directories: data_inc, dependencies: directfb_dep,    install: true)
executable('df_video_sample', 'df_video_sample.c', include_directories: data_inc, dependencies: directfb_dep,    install: true)

if fusionsound_dep.found()
executable('fs_music_sample', 'fs_music_sample.c',                                dependencies: fusionsound_dep, install: true)
endif

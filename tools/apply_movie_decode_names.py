"""
Apply the movie decode pipeline names from reversing/MOVIE_DECODE.md to the FFX.exe IDB.

PROGRESS 1.9.6. Three tables:

    NAMES    {ea: (name, comment)}           functions that only had a placeholder name
    RENAMES  {ea: (new, "was OLD. why")}     functions whose current name is wrong or vague
    GLOBALS  {ea: (name, comment)}           data

How the third party code was matched, so nobody has to take the names on trust:

- libwebm mkvparser. Every function named here either carries an assert string that names
  "..\\third_party\\libwebm\\mkvparser.cpp" plus an expression that only one upstream function
  asserts ("pReader" and "pos >= 0" is Segment::CreateInstance, "m_clusters == NULL" then
  "m_clusterSize == 0" is Segment::Load, "m_timecodeScale >= 1" is the inlined
  SegmentInfo::GetDuration, "idx < m_frame_count" is Block::GetFrame, "m_entries[idx] == pCurr" is
  Cluster::GetNext), or it is a one line accessor whose return type and caller pin it
  (GetFrameRate returns a double and the player falls back to 29.97 when it is 0, GetType returns 1
  or 2 and the reader picks the video and audio track on it).
- libvpx 1.4.0 (the string "WebM Project VP8 Decoder v1.4.0" sits in the iface at 0xB7F728, which
  0x40A430 returns). vpx_codec_dec_init_ver checks ver == 9 (VPX_DECODER_ABI_VERSION in 1.4) and
  iface->abi_version == 5, vpx_codec_decode returns VPX_CODEC_INVALID_PARAM (8) on the null data
  checks and calls iface slot +32, get_frame calls +36, destroy calls +16. vpx_mem_set_functions
  allocates the 28 byte, 7 pointer GLOBAL_FUNC_POINTERS block.
- libvorbis 1.3.5 ("Xiph.Org libVorbis 1.3.5" and "Xiph.Org libVorbis I 20150105"). Matched by
  struct sizes (vorbis_info_init callocs codec_setup_info 0xE50, vorbis_block_init and
  vorbis_block_clear memset 0x70), by return codes (headerin -133 OV_EBADHEADER and -132
  OV_ENOTVORBIS after the "vorbis" compare, synthesis_read -131 OV_EINVAL) and by the call order
  in the player's audio open and PCM fill.
- libogg bitwise.c. oggpack_read and oggpack_write by the mask table and the 256 byte
  BUFFER_INCREMENT realloc.

The FFX_ and PVideoPlaybackWin32_ names come from reading the bodies, see the doc.

It refuses a NAMES entry whose target is not a function start or whose current name is not a
placeholder (sub_, nullsub_, j_sub_, unknown_libname_, mkvparser__f, <Class>__vfNN), a RENAMES entry
whose current name is not exactly OLD, and any new name that already exists at another address.
Data names are refused unless the current name is a dword_/off_/unk_/flt_/byte_ placeholder.

DRY_RUN is True by default and the script never saves. Run it in IDA, or headless on a COPY:

    python apply_movie_decode_names.py --idb path/to/copy/FFX.exe.i64 [--apply] [--verbose]

Headless runs close the database without saving, so --apply there is only a rehearsal.
"""

import re
import sys

DRY_RUN = True
VERBOSE = False

NAMES = {
    # PVideoPlaybackWin32, the Virtuos Win32 player. The SDK's own Win32 backend is an empty stub,
    # so this one is game specific. The interface names (isPlayerActive, getCurrentTime, jumpTo,
    # pause, setVolume, stop, terminate) follow the Phyre PVideoPlayback interface they implement.
    0xA26FB0: ("PVideoPlaybackWin32_vpxMemcpy", "memcpy shim handed to vpx_mem_set_functions by startVideoDecodingThread."),
    0xA26FC0: ("PVideoPlaybackWin32_vpxMemmove", "memmove shim handed to vpx_mem_set_functions by startVideoDecodingThread."),
    0xA26FD0: ("PVideoPlaybackWin32_vpxMemset", "memset shim handed to vpx_mem_set_functions by startVideoDecodingThread."),
    0xA26FE0: ("PVideoPlaybackWin32_copyPlane", "(dst, dstPitch, src, srcPitch, rowBytes, rows): row by row plane copy, one memcpy when the pitches match. Used by ConsumeFrameToTextures for the Y, U and V planes."),
    0xA27170: ("PVideoPlaybackWin32_fillPcm", "The Vorbis decode. Called from the FMOD pcmreadcallback, so it runs on FMOD's stream thread. Returns at once while the prebuffer byte +0x2A8 is set. Otherwise fills +0x2A4 (4096) frames: vorbis_synthesis_pcmout, float to s16 with clipping, vorbis_synthesis_read, and when the dsp is dry pops the next packet off the audio queue +0x80 and runs vorbis_synthesis + blockin."),
    0xA273B0: ("PVideoPlaybackWin32_openAudio", "Audio half of play(). Splits the A_VORBIS CodecPrivate (Xiph lacing) into the three header packets, vorbis_synthesis_headerin x3, synthesis_init, block_init, the 1 MiB audio packet queue at +0x80, then FMOD createSound(mode 0x4C8 = OPENUSER|CREATESTREAM|SOFTWARE|2D, PCM16, decodebuffersize 4096, pcmreadcallback PVideoPlaybackWin32_fmodPcmReadCallback) and playSound. Runs on the caller of play(), the step thread for a script movie."),
    0xA27650: ("PVideoPlaybackWin32_readAllBlocks", "The reading thread's work. Walks Segment clusters and block entries, and for each frame of the video track reserves a slot in the packet queue +0 (audio track: queue +0x80) and fills it with VbfMkvReader::Read. Blocks when a queue is full. Loops back to the first cluster when the loop flag +0xF0 is set. Ends by closing both packet queues and setting the done event +0x2B4."),
    0xA278F0: ("PVideoPlaybackWin32_closeAudio", "Stops and releases the FMOD channel and sound (spins with Sleep(1) until the stream reports ready), clears the Vorbis state and frees the audio packet queue."),
    0xA279B0: ("PVideoPlaybackWin32_decodeLoop", "The video decoding thread's work. Pops a VP8 packet from queue +0, vpx_codec_decode, vpx_codec_get_frame, reserves a frame slot (blocks while full) in the decoded queue +0x40 and copies the I420 planes in. When the packet queue closes it closes the decoded queue and clears the decoder running byte +0x154."),
    0xA27BD0: ("PVideoPlaybackWin32_getCurrentTime", "(consumed frames +0x100, plus 1) times the frame period +0x108, in ms. A count of consumed frames, NOT a clock."),
    0xA27C40: ("PVideoPlaybackWin32_getEndTime", "Returns +0xF8, the container duration in ms (Segment::GetDuration / 1e6)."),
    0xA27C50: ("PVideoPlaybackWin32_isPlayerActive", "True while the decoder thread runs (+0x154), or an audio track exists and the FMOD stream is not READY, or a decoded frame is still queued."),
    0xA27CE0: ("PVideoPlaybackWin32_getStartTime", "Returns 0."),
    0xA27E10: ("PVideoPlaybackWin32_jumpTo", "Stub, returns 1. So the seek the skip handler issues does nothing."),
    0xA27E20: ("maybe_PVideoPlaybackWin32_mapTextureForWrite", "Maps the next buffer of a triple buffered PTexture2D for write (PTexture2DD3D11__mapForWrite). Used by ConsumeFrameToTextures before each plane copy."),
    0xA27E90: ("PVideoPlaybackWin32_pause", "Toggle. State 1 -> 2 pauses the FMOD channel, 2 -> 1 resumes it. ConsumeFrameToTextures refuses while the state is not 1."),
    0xA27EE0: ("PVideoPlaybackWin32_fmodPcmReadCallback", "FMOD_CREATESOUNDEXINFO.pcmreadcallback. Recovers the player from Sound::getUserData and calls PVideoPlaybackWin32_fillPcm. Runs on FMOD's stream thread inside fmodex.dll."),
    0xA28330: ("PVideoPlaybackWin32_setVolume", "FMOD::Channel::setVolume on the movie channel."),
    0xA28350: ("PVideoPlaybackWin32_stop", "State 3, decoder stop flag, close packet queue, wait for the reader done event, delete the Segment, close the reader, close the decoded queue, join the decode thread, vpx_codec_destroy, vpx_mem_unset_functions, close audio, free both video queues."),
    0xA28430: ("PVideoPlaybackWin32_terminate", "stop, then asks the reading thread to quit (+0xD0 = 1, wake event) and joins it."),
    # CDataFrameQueue, a blocking single producer single consumer ring of length prefixed frames.
    0xA3DD90: ("CDataFrameQueue__beginRead", "Front of the queue. BLOCKS (WaitForSingleObject INFINITE on the data event +0x24) while the queue is empty and still open. Returns 0 only once the queue has been closed and drained. Returns the [u32 size][bytes] slot."),
    0xA3DDC0: ("CDataFrameQueue__endRead", "Pop: read offset = pending, ++popped (+0x34), signal the space event +0x1C if a writer may be waiting."),
    0xA3DDE0: ("CDataFrameQueue__count", "pushed (+0x38) minus popped (+0x34), 0 when not initialised."),
    0xA3DDF0: ("CDataFrameQueue__init", "(capacity, maxFrame, alloc, free): two auto reset events (+0x1C space, initially set, +0x24 data), buffer of capacity + 8 + maxFrame + 3, open byte +0x2C."),
    0xA3DEA0: ("CDataFrameQueue__close", "End of stream: clears the open byte +0x2C and sets both events, so a blocked reader or writer wakes and gets 0."),
    0xA3DEC0: ("CDataFrameQueue__beginWrite", "Reserve a slot of n bytes. BLOCKS on the space event +0x1C while full and open. Writes the size word and returns the slot."),
    0xA3DF00: ("CDataFrameQueue__endWrite", "Commit: write offset = pending, ++pushed (+0x38), set the data event +0x24."),
    0xA3DF20: ("CDataFrameQueue__release", "Frees the buffer through the stored free function and closes both events."),
    0xA3DFB0: ("CDataFrameQueue__waitForSpan", "Shared wait: loops until pos + size + 4 fits before the other side's offset (unwrapped by capacity), waiting on the given event while the queue is open. Returns 0 when closed."),
    # VbfMkvReader, the libwebm IMkvReader over the archive.
    0xA3E030: ("VbfMkvReader__ctor", "Sets the VbfMkvReader vtable and clears the file handle at +0x10."),
    0xA3E040: ("VbfMkvReader__destroy", "Non deleting destructor body: frees the FFX_fios_FileHandle at +0x10, then the IMkvReader base."),
    0xA3E150: ("VbfMkvReader__Close", "Frees the FFX_fios_FileHandle at +0x10."),
    0xA3E1F0: ("VbfMkvReader__Open", "(path): FFX_fios_FileHandle(path, readOnly 1). The handle goes to the VBF first (FFX_VbfManager__openFileStream) and to the loose file system on a miss. Stores the 64 bit size at +8."),
    # libwebm mkvparser accessors and walkers that were still sub_ placeholders.
    0x4251B0: ("mkvparser__Segment__GetTracks", "libwebm. Returns m_pTracks (+0x74)."),
    0x429DE0: ("mkvparser__Tracks__GetTrackByIndex", "libwebm. Bounds checked index into m_trackEntries."),
    0x429DA0: ("mkvparser__Tracks__GetTrackByNumber", "libwebm. Linear search of m_trackEntries on Track number (+0x24)."),
    0x427B10: ("mkvparser__Track__GetType", "libwebm. 1 video, 2 audio."),
    0x427B80: ("mkvparser__Track__GetCodecPrivate", "libwebm. Returns the CodecPrivate pointer, size through the out argument."),
    0x428DB0: ("mkvparser__VideoTrack__GetWidth", "libwebm."),
    0x428DC0: ("mkvparser__VideoTrack__GetHeight", "libwebm."),
    0x428DD0: ("mkvparser__VideoTrack__GetFrameRate", "libwebm. 0 for every shipped FFX movie (no FrameRate element), so the player falls back to 29.97002997."),
    0x429170: ("mkvparser__AudioTrack__GetSamplingRate", "libwebm. 48000 for the shipped movies."),
    0x429180: ("mkvparser__AudioTrack__GetChannels", "libwebm. 2 for the shipped movies."),
    0x42B830: ("mkvparser__Cluster__EOS", "libwebm. m_pSegment == NULL."),
    0x42D050: ("mkvparser__BlockEntry__EOS", "libwebm. GetKind() == kBlockEOS."),
    0x42E060: ("mkvparser__Block__GetTrackNumber", "libwebm."),
    0x42E0C0: ("mkvparser__Block__GetFrameCount", "libwebm."),
    0x41E6A0: ("mkvparser__IMkvReader__dtor", "libwebm. Non deleting body, resets the vtable."),
    0x41FD10: ("maybe_mkvparser__Segment__delete", "~Segment then the Virtuos mkv free pointer g_mkvFree. Called by PVideoPlaybackWin32_stop."),
    0x41E5B0: ("FFX_Mkv_SetAllocators", "Virtuos addition to libwebm: sets g_mkvMalloc and g_mkvFree. Called by startVideoDecodingThread."),
    0x41E5D0: ("FFX_Mkv_ResetAllocators", "Restores the CRT malloc and free into g_mkvMalloc and g_mkvFree."),
    # libvpx 1.4.0.
    0x4094C0: ("vpx_codec_dec_init_ver", "libvpx 1.4.0. ver must be 9. The only caller (startVideoDecodingThread 0xA27FBA) passes cfg NULL and flags 0, so cfg.threads is 0 and vp8_decoder_create_threads makes no workers."),
    0x409680: ("vpx_codec_decode", "libvpx 1.4.0."),
    0x4096E0: ("vpx_codec_get_frame", "libvpx 1.4.0."),
    0x409370: ("vpx_codec_destroy", "libvpx 1.4.0."),
    0x40A430: ("vpx_codec_vp8_dx", "libvpx 1.4.0. Returns the VP8 decoder iface at 0xB7F728."),
    0x40A5D0: ("vpx_mem_set_functions", "libvpx 1.4.0 (USE_GLOBAL_FUNCTION_POINTERS). malloc, calloc, realloc, free, memcpy, memset, memmove."),
    0x40A630: ("vpx_mem_unset_functions", "libvpx 1.4.0."),
    0x40A680: ("maybe_create_decompressor", "libvpx onyxd_if.c create_decompressor, by its one caller vp8_create_decoder_instances."),
    # libvorbis 1.3.5.
    0xA8FFD0: ("vorbis_info_init", "libvorbis 1.3.5."),
    0xA8FD80: ("vorbis_info_clear", "libvorbis 1.3.5."),
    0xA90420: ("vorbis_comment_init", "libvorbis 1.3.5."),
    0xA90030: ("vorbis_comment_clear", "libvorbis 1.3.5."),
    0xA8F650: ("vorbis_synthesis_headerin", "libvorbis 1.3.5."),
    0xA910E0: ("vorbis_synthesis_init", "libvorbis 1.3.5."),
    0xA921B0: ("vorbis_block_init", "libvorbis 1.3.5."),
    0xA92040: ("vorbis_block_clear", "libvorbis 1.3.5."),
    0xA917A0: ("vorbis_dsp_clear", "libvorbis 1.3.5."),
    0xA92460: ("vorbis_synthesis", "libvorbis 1.3.5."),
    0xA90A70: ("vorbis_synthesis_blockin", "libvorbis 1.3.5."),
    0xA90A10: ("vorbis_synthesis_pcmout", "libvorbis 1.3.5."),
    0xA909E0: ("vorbis_synthesis_read", "libvorbis 1.3.5."),
    0xA8F290: ("_vorbis_pack_books", "libvorbis 1.3.5 info.c, encoder side, linked but unused by the player."),
    0xA8F570: ("_vorbis_pack_info", "libvorbis 1.3.5 info.c, encoder side."),
    0xA8F4C0: ("_vorbis_pack_comment", "libvorbis 1.3.5 info.c, writes the vendor string."),
    # libogg.
    0x42EB10: ("oggpack_read", "libogg bitwise.c."),
    0x42E350: ("oggpack_write", "libogg bitwise.c."),
    0x42E1D0: ("FFX_Ogg_SetAllocators", "Virtuos addition: points g_oggMalloc, g_oggCalloc, g_oggRealloc, g_oggFree at the given functions. Called by PVideoPlaybackWin32_openAudio."),
    0x42E200: ("FFX_Ogg_ResetAllocators", "Restores the CRT allocators. Called by PVideoPlaybackWin32_closeAudio."),
    # FFX side of the FMV manager.
    0x6D7430: ("FFX_FmvMgr_StartOrResume", "Body of FFX_Fmv_SetFullscreenMode. First call latches the fullscreen byte +0x6FD and, when a path is set, calls FFX_FmvMgr_PlayPath, which creates the decode thread. Once the first frame is up (+0x6E4) it only unpauses (+0x6FC) and restores the volume. Reached from movie:0 start, movie:9 resi and movie:10 start, so it runs on the step thread."),
    0x6D8FC0: ("FFX_FmvMgr_PlayPath", "(path, loop, fullscreen): zeroes the frame index pair, PVideoPlaybackWin32__startVideoDecodingThread on mgr+0x38 (a -3, file not opened, raises a message box), optionally sets the volume, then IsPlaying (+0x6D0) = 1."),
    0x6D8F30: ("FFX_FmvMgr_OpenPlayer", "startReadingThread on mgr+0x38 with prebuffer 30 and the FmodManager audio flag, then HasPlayer (+0x6D2) = 1. Called at the end of FFX_Fmv_CreatePlayer."),
    0x6D8F80: ("FFX_FmvMgr_Unpause", "PVideoPlaybackWin32_pause toggle and Paused (+0x6FC) = 0."),
    0x6D8CB0: ("FFX_FmvMgr_ProcessCommands", "Per frame command list at mgr+4/+0xC (args +0x10/+0x18). Called from FFX_Frame_UpdateBoostersAndOverlays on the main thread. The list is only ever pushed from here (command 3 pushes 4), so in the shipped game it is always empty."),
    0x6DADF0: ("j_FFX_FmvMgr_ProcessCommands", "Thunk. IDA renames it by itself once 0x6D8CB0 is named, so an --apply rehearsal reports one fewer than the dry run."),
    0x6D7D90: ("FFX_FmvMgr_PushCommand", "(cmd, arg) push onto the manager's command list. Only caller is FFX_FmvMgr_ProcessCommands."),
    0x6DA460: ("FFX_Fmv_OnVideoShaderLoaded", "AsyncLoadManager callback for Video.cgfx, on the AsyncLoad worker. Stores the cluster at mgr+0x360 and calls FFX_Fmv_BindVideoMaterial."),
    0x6D86E0: ("FFX_Fmv_BindVideoMaterial", "Binds the YChannel, UChannel and VChannel samplers to the three plane textures, reads imageWidth and imageHeight, builds the mesh instance at mgr+0x388."),
    0x6D91B0: ("FFX_FmvMgr_GetFrameIndex", "Returns mgr+0x6D8, the presented frame index."),
    0x6D91C0: ("FFX_FmvMgr_GetCurrentTimeMs", "PVideoPlaybackWin32_getCurrentTime on the manager's player. No caller."),
    0x6D91D0: ("FFX_FmvMgr_GetLengthFrames", "(int)(getEndTime * 29.97 / 1000). movie:12."),
    0x6D9220: ("FFX_FmvMgr_GetEndTimeMs", "PVideoPlaybackWin32_getEndTime. No caller."),
    0x645DB0: ("FFX_Fmv_IsPaused", "Returns mgr+0x6FC."),
    0x6437F0: ("FFX_Fmv_SetStopSuppressed", "mgr+0x720 = (arg != 0). movie:0 with flag 0x2000 passes 1, movie:4 start passes 0."),
    0x63DF30: ("FFX_Fmv_DestroyPlayerGlobal", "FFX_Fmv_DestroyPlayer on the global manager. movie:4 getter and FFX_Fmv_AbortForSceneChange."),
    # 0x6DABF0 is FFX_Fmv_SetVolume, named by apply_sound_names.py. It stores g_ffxFmvVolume and applies it while a script movie plays.
    0x643E50: ("FFX_Fmv_ClearIsPlaying", "mgr+0x6D0 = 0 and nothing else. The movie:4 poll calls it. The IDB label FFX_Fmv_StopPlaying sits on its tail chunk at 0x6DAC40, not on the function."),
    0x6DA9B0: ("maybe_FFX_FmvMgr_PlayFromJob", "Like FFX_FmvMgr_PlayPath but takes a {path, loop, mute} block and sets mgr+0x704. No reference of any kind, dead."),
}

RENAMES = {
    0xA3E2B0: ("VbfMkvReader__Read", "was VbfMkvReader__scalar_deleting_dtor_1. Slot 0 of IMkvReader is Read(long long pos, long len, unsigned char *buf). The body range checks pos and len, seeks the FFX_fios_FileHandle and returns -(bytesRead < len). It is not a destructor."),
    0x41E6B0: ("mkvparser__IMkvReader__scalar_deleting_dtor", "was IMkvReader__vf02, a mechanical slot name. Resets the vtable and frees through g_mkvFree when flag 1 is set."),
    0x41FD50: ("mkvparser__Segment__CreateInstance", "was mkvparser__f41FD50, a module placeholder. Asserts pReader and pos >= 0 at mkvparser.cpp 0x282/0x283 and writes *pSegment."),
    0x421450: ("mkvparser__Segment__Load", "was mkvparser__f421450, a module placeholder. Asserts m_clusters == NULL, m_clusterSize == 0, m_clusterCount == 0, then ParseHeaders and the LoadCluster loop."),
    0x425200: ("mkvparser__Segment__GetDuration", "was mkvparser__f425200, a module placeholder. Asserts m_pInfo, inlines SegmentInfo::GetDuration (m_timecodeScale >= 1)."),
    0x423840: ("mkvparser__Segment__GetFirst", "was mkvparser__f423840, a module placeholder. Returns &m_eos when there are no clusters, asserts pCluster."),
    0x4238D0: ("mkvparser__Segment__GetNext", "was mkvparser__f4238D0, a module placeholder. Segment::GetNext(const Cluster *)."),
    0x4294C0: ("mkvparser__Tracks__GetTracksCount", "was mkvparser__f4294C0, a module placeholder. Asserts result >= 0."),
    0x42C880: ("mkvparser__Cluster__GetFirst", "was mkvparser__f42C880, a module placeholder. Asserts m_entries and pFirst."),
    0x42C9D0: ("mkvparser__Cluster__GetNext", "was mkvparser__f42C9D0, a module placeholder. Asserts pCurr and m_entries[idx] == pCurr."),
    0x42E0D0: ("mkvparser__Block__GetFrame", "was mkvparser__f42E0D0, a module placeholder. Asserts idx < m_frame_count, f.pos > 0, f.len > 0."),
    0x40AE10: ("vp8_create_decoder_instances", "was vp8_dx_init. The body is libvpx vp8_dx_iface.c vp8_create_decoder_instances: if !fb->use_frame_threads, pbi[0] = create_decompressor, pbi->max_threads = oxcf->max_threads, vp8_decoder_create_threads. vp8_init is a different function."),
    0x641CE0: ("FFX_Fmv_IsStartPending", "was FFX_Fmv_IsDecoderBusy. It never looks at the decoder. It is FFX_FmvMgr_IsStartPending on the global manager: a queued play command or mgr+0x704 == 0, and in the shipped game neither can happen, so it returns 0."),
    0x6DA2A0: ("FFX_FmvMgr_IsStartPending", "was FFX_Fmv__isDecoderBusy. True when the command list at mgr+4/+0xC holds a 1 (play) or mgr+0x704 is 0. The list is only pushed by FFX_FmvMgr_ProcessCommands itself and mgr+0x704 is only ever written with 1 (constructor and the dead maybe_FFX_FmvMgr_PlayFromJob), so it is constant 0."),
    0x645DC0: ("FFX_Fmv_FirstFrameShownOrStopped", "was FFX_Fmv_PlaybackFinished. Returns mgr+0x6E4 while IsPlaying, which is set after the first consumed frame, and 1 when nothing plays. It says nothing about the end of a movie (ATEL_MOVIE_SAVE_LIBRARIES.md 1.4)."),
}

GLOBALS = {
    0x1A84C80: ("g_ffxFmvDecodeStartTick", "GetTickCount at 0xA27F69 in startVideoDecodingThread. Written there and read by nothing, so the decoder has no clock."),
    0x1984C80: ("g_ffxFmvDiscardBlockBuffer", "1 MiB scratch the reading thread reads audio blocks into when there is no audio queue, so they are read and dropped."),
    0x22FB508: ("g_vpxMemFuncs", "libvpx vpx_mem g_func, the 7 pointer block vpx_mem_set_functions allocates."),
    0x22FB510: ("g_mkvMalloc", "Virtuos libwebm allocator hook, set by FFX_Mkv_SetAllocators."),
    0x22FB514: ("g_mkvFree", "Virtuos libwebm free hook, set by FFX_Mkv_SetAllocators."),
    0xC8B240: ("g_oggFree", "libogg _ogg_free as a pointer (Virtuos)."),
    0xC8B244: ("g_oggRealloc", "libogg _ogg_realloc as a pointer (Virtuos)."),
    0xC8B248: ("g_oggMalloc", "libogg _ogg_malloc as a pointer (Virtuos)."),
    0xC8B24C: ("g_oggCalloc", "libogg _ogg_calloc as a pointer (Virtuos)."),
    0xC34420: ("g_ffxFmvVolume", "Movie volume, written by FFX_Fmv_SetVolume, applied by FFX_FmvMgr_StartOrResume."),
    0xC6B274: ("g_ffxFmvAllocVpxMalloc", "First of the allocator pointers startVideoDecodingThread hands to vpx_mem_set_functions."),
    0xC6B284: ("g_ffxFmvAllocMkvMalloc", "Allocator pair handed to FFX_Mkv_SetAllocators (with +4 free)."),
    0xC6B28C: ("g_ffxFmvAllocOggMalloc", "Allocator quad handed to FFX_Ogg_SetAllocators (malloc, calloc, realloc, free)."),
    0xC6B29C: ("g_ffxFmvAllocQueueAlloc", "Allocator pair handed to CDataFrameQueue__init (with +4 free)."),
}

# Function comments that are wrong today. {ea: (expected name after RENAMES, new comment)}. The new
# text replaces both the repeatable and the non repeatable function comment. Each line stays under
# IDA's 1024 character cap.
COMMENTS = {
    0x641690: ("FFX_Fmv_StartVideo",
               "FFX_Fmv_CreatePlayer on the global manager, folded to 0 or 1 by neg/sbb/neg at 0x6416AF.\n"
               "CreatePlayer returns 0 on success AND when the video id has no path (both reach xor eax,eax at 0x6DA184,\n"
               "the miss through the jmp at 0x6D9E19). It returns nonzero only when sub_58CF80, the sampler state create,\n"
               "fails (jnz at 0x6D9E67). So this returns 1 only on that failure. Movie:10 start tests it with jz at\n"
               "0x76EBB4, so on every normal start it skips SetFullscreenMode, ScriptRunning and Blocking, the decode\n"
               "never starts and its poll finishes at once. A missing file is not reported here at all.\n"
               "See reversing/MOVIE_DECODE.md."),
    0x645DC0: ("FFX_Fmv_FirstFrameShownOrStopped",
               "Returns mgr+0x6E4 while mgr+0x6D0 (IsPlaying) is set, else 1. mgr+0x6E4 is stored 1 after every\n"
               "successful upload (0x6D7828 in FFX_Fmv_StepPresentation, and in FFX_Fmv_UploadFrameJob), so this is 1\n"
               "from the first presented frame on. The movie:9 and movie:10 polls use it, so those waits end on the\n"
               "first frame, not the last. Clearing IsPlaying (the skip, the natural end) also reads as 1.\n"
               "See reversing/MOVIE_DECODE.md and FMV_SYNC.md (correction 2026-10-13)."),
    0x641CE0: ("FFX_Fmv_IsStartPending",
               "FFX_FmvMgr_IsStartPending on the global manager. It does not look at the decoder. True only when the\n"
               "manager's command list holds a play command or mgr+0x704 is 0, and neither happens in the shipped game\n"
               "(the list is only pushed by FFX_FmvMgr_ProcessCommands itself, mgr+0x704 is only written with 1), so it\n"
               "is constant 0. The movie:1, 9 and 10 polls and the kit's FmvDecoderBusy read it.\n"
               "See reversing/MOVIE_DECODE.md."),
    0x6D7680: ("FFX_Fmv_StepPresentation",
               "Per step video presentation, called from FFX_Fmv_StepFromMainStep at the top of FFX_MainStep, step thread.\n"
               "Runs while HasPlayer (mgr+0x6D2) and (no frame shown yet, or not paused mgr+0x6FC).\n"
               "Keep presenting while isPlayerActive and (loop mgr+0x6FE, or mgr+0x6E4 == 0, or (getCurrentTime < getEndTime\n"
               "and no skip)). Then a 0x24 job: when mgr+0x570 is set (videos 46, 72, 32 and looping movies) it goes to the\n"
               "Phyre render thread as FFX_Fmv_UploadFrameJob, otherwise inline: if IsPlaying and\n"
               "PVideoPlaybackWin32_ConsumeFrameToTextures (which BLOCKS on an empty decoded queue once the 30 frame\n"
               "prebuffer is met), mgr+0x6E0 = old index, mgr+0x6D8 = (int)((float)curMs * 29.97f * 0.001f), and\n"
               "mgr+0x6E4 = 1 at 0x6D7828. mgr+0x6E4 means 'a frame has been shown', not 'playback complete'.\n"
               "Afterwards, if a frame is shown, the fullscreen latch mgr+0x6FD is set, g_ffxFmvScriptRunning is 0 and it\n"
               "is not paused, it pauses the player and sets mgr+0x6FC: the first frame hold that movie:9 resi releases.\n"
               "Stop branch (not active, or the end or skip test failed, and a frame was shown and mgr+0x720 is 0):\n"
               "mgr+0x6D8 = 65534, IsPlaying = 0, mgr+0x710 = 1.\n"
               "See reversing/MOVIE_DECODE.md and FMV_SYNC.md."),
    0x6D9C80: ("FFX_Fmv_CreatePlayer",
               "PhyFMVPlayerManager create player. Resolves the video id through the FFX_VideoList map to a path at\n"
               "mgr+0x4F0, clears g_ffxFmvSkipRequested, stores the loop flag (mgr+0x6FE) and the render thread upload\n"
               "flag (mgr+0x570, videos 46, 72, 32 or loop), builds the Y, U and V textures, queues the Video.cgfx load\n"
               "(callback FFX_Fmv_OnVideoShaderLoaded on the AsyncLoad worker) and calls FFX_FmvMgr_OpenPlayer, which\n"
               "only starts the reading thread. Decode starts later, in FFX_FmvMgr_PlayPath. Returns 0 on success and\n"
               "on a missing path, nonzero only when sub_58CF80 fails.\n"
               "The decoder has no clock: the GetTickCount store at 0xA27F69 is never read, and frames are consumed one\n"
               "per FFX_MainStep. See reversing/MOVIE_DECODE.md."),
}

PLACEHOLDER = re.compile(r"^(sub_|nullsub_|j_sub_|unknown_libname_|mkvparser__f[0-9A-F]+$|\w+__vf\d+$)")
DATA_PLACEHOLDER = re.compile(r"^(dword_|off_|unk_|flt_|byte_|qword_|word_|stru_)")


def run(idc, ida_funcs, apply):
    ok = skip = 0

    def taken(name, ea):
        other = idc.get_name_ea_simple(name)
        return other != idc.BADADDR and other != ea

    def do(ea, new, cmt, func):
        nonlocal ok
        if apply:
            idc.set_name(ea, new, idc.SN_NOWARN | idc.SN_NOCHECK)
            if func:
                idc.set_func_cmt(ea, cmt, 1)
            else:
                idc.set_cmt(ea, cmt, 1)
        ok += 1
        if VERBOSE or not apply:
            print("%s %08X %s -> %s" % ("apply" if apply else "would", ea, idc.get_name(ea) if not apply else "", new))

    for ea, (new, cmt) in sorted(NAMES.items()):
        f = ida_funcs.get_func(ea)
        cur = idc.get_name(ea)
        if f is None or f.start_ea != ea:
            print("skip %08X %s: not a function start" % (ea, new)); skip += 1; continue
        if cur == new:
            continue
        if not PLACEHOLDER.match(cur or "sub_"):
            print("skip %08X %s: current name %s is not a placeholder" % (ea, new, cur)); skip += 1; continue
        if taken(new, ea):
            print("skip %08X %s: name exists at %08X" % (ea, new, idc.get_name_ea_simple(new))); skip += 1; continue
        do(ea, new, cmt, True)

    for ea, (new, why) in sorted(RENAMES.items()):
        cur = idc.get_name(ea)
        m = re.match(r"^was (\S+?)[.,]", why)
        old = m.group(1) if m else ""
        if cur == new:
            continue
        if cur != old:
            print("skip %08X %s: current name %s, expected %s" % (ea, new, cur, old)); skip += 1; continue
        if taken(new, ea):
            print("skip %08X %s: name exists at %08X" % (ea, new, idc.get_name_ea_simple(new))); skip += 1; continue
        old_cmt = "" if ea in COMMENTS else (idc.get_func_cmt(ea, 1) or "")
        cmt = why if not old_cmt else (why + "\n" + old_cmt)
        do(ea, new, cmt[:1024], True)

    for ea, (new, cmt) in sorted(GLOBALS.items()):
        cur = idc.get_name(ea)
        if cur == new:
            continue
        if cur and not DATA_PLACEHOLDER.match(cur):
            print("skip %08X %s: current name %s is not a data placeholder" % (ea, new, cur)); skip += 1; continue
        if taken(new, ea):
            print("skip %08X %s: name exists at %08X" % (ea, new, idc.get_name_ea_simple(new))); skip += 1; continue
        do(ea, new, cmt, False)

    for ea, (name, cmt) in sorted(COMMENTS.items()):
        cur = idc.get_name(ea)
        expect = {name}
        for rea, (new, why) in RENAMES.items():
            if rea == ea and new == name:
                m = re.match(r"^was (\S+?)[.,]", why)
                if m and not apply:
                    expect.add(m.group(1))
        if cur not in expect:
            print("skip comment %08X: current name %s, expected %s" % (ea, cur, name)); skip += 1; continue
        if any(len(line) > 1000 for line in cmt.split("\n")):
            print("skip comment %08X: a line is over 1000 characters" % ea); skip += 1; continue
        if apply:
            idc.set_func_cmt(ea, cmt, 1)
            idc.set_func_cmt(ea, "", 0)
        ok += 1
        if VERBOSE or not apply:
            print("%s comment %08X %s" % ("apply" if apply else "would", ea, name))

    print("%s %d, skipped %d" % ("applied" if apply else "would apply", ok, skip))


def main(argv):
    global VERBOSE
    apply = not DRY_RUN
    idb = None
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--idb":
            idb = argv[i + 1]; i += 1
        elif a == "--apply":
            apply = True
        elif a == "--verbose":
            VERBOSE = True
        i += 1
    if idb:
        import idapro
        idapro.open_database(idb, run_auto_analysis=False)
    import idc
    import ida_funcs
    try:
        run(idc, ida_funcs, apply)
    finally:
        if idb:
            idapro.close_database(save=False)


if __name__ == "__main__":
    main(sys.argv[1:])

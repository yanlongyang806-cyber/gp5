Cryptic 3rdparty zlib 1.2.5, with Kevin Day's deflateSetRsyncParameters()


To recompile:

1) Compile 32-bit asm:
  A. Use Visual Studio Command Prompt (2010)
  B. Go to C:\src\3rdparty\zlib\contrib\masmx86
  C. Run bld_ml32.bat
2) Compile 64-bit asm:
  A. Use Visual Studio x64 Win64 Command Prompt (2010)
  B. Go to C:\src\3rdparty\zlib\contrib\masmx64
  C. Run bld_ml64.bat
3) Open C:\src\3rdparty\zlib\contrib\vstudio\vc10\zlibvc.sln
4) Build 'zlibstat' in all configurations (Don't use IncrediBuild!  It will screw everything up!)
5) Tweak capitalization of .pdb's in bin if necessary
6) Test compile in APMS in all configurations
7) Verify proper zlib operation with pig create and verify operations
8) Check in


General Notes:

1) The original distributed files are in Cryptic\Originals.
2) All of the files that didn't seem necessary for rebuilding and general debugging were put in Cryptic\Cleanup.zip, to limit the number of files in Subversion.
3) The project file was adjusted in these ways:
  A. Remove Itanium
  B. Adjust output paths
  C. Set static debug CRT
  D. Remove ZLIB_WINAPI define
  E. Fix miscellaneous compile warnings
4) Kevin Day's rsyncable_checksum.patch is manually ported and applied.
  A. Miscellaneous porting
  B. RSYNC_DEFAULT_CHECKSUM_TYPE changed to Z_RSYNCABLE_OFF
  C. deflateBound() padded when rsync is on

On rsyncable_checksum.patch:

The patch comes from a Feb 18, 2005 post on the rsync mailing list by Kevin Day, in which he describes some improvements he has made to a more basic patch.  I'm not certain of that basic patch's origin, but it is obviously descended in some way from Rusty Russell's circa 2001 patch to add --rsyncable to gzip.  It's possible the intermediate versions came from the UHU-Linux project.  Rusty's original patch is based on some comments in "Efficient Algorithms for Sorting and Synchronization," Andrew Tridgell's PhD thesis, about how to adapt compressed data to be compatible with rsync.

hoglib used zipDataChunked(), which does a full zlib sync at fixed intervals, attempting to improve the bindiffability of compressed data.  This is mostly ineffective, because any fixed offset will misalign blocks; pretty much only data before the offset change will be the same in the compressed output.  The solution this patch implements is to emit the full sync markers whenever a portion of the file is reached which has particular, random characteristics, allowing the bindiff to pick back up some time after the changed section, even if there are offsets.  The patch uses a masked portion of a byte-wise rolling checksum as an indicator for this purpose.

The patch is designed for 1.2.2, and did not apply cleanly, but the differences appear to be small enough that I'm reasonably confident my basic port.  Verify testing with pig and testing in the patch system, which has extensive verify mechanisms, have given me reason to believe the zlib streams emitted with this patch are correct.

I discovered some problems with the patch in testing.  First, deflateBound() does not account for the size of flush points, meaning it actually can actually return a length that is not sufficient.  This is most likely to happen with data that is not compressable.  Secondly, compressed files seem to have a cluster of flush points near the beginning of them in rapid sequence.  Less seriously, the density of the points seems to be several times higher in testing than the reset block size would seem to suggest.  This situation is mitigated by requiring a minimum block size before subsequent flushes are emitted.  I have implemented this as an auxiliary to the original patch.  These issues might suggest deeper problems with the patch, but it seems to work well in practice.  In any case, I suspect that the deflateBound() issue is just an oversight.

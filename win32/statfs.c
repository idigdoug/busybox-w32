#include <sys/statfs.h>
#include "libbb.h"
#include "strconv.h"

/*
 * Code from libguestfs (with addition of GetVolumeInformation call)
 */
int statfs(const char *file, struct statfs *buf)
{
	ULONGLONG free_bytes_available;         /* for user - similar to bavail */
	ULONGLONG total_number_of_bytes;
	ULONGLONG total_number_of_free_bytes;   /* for everyone - bfree */
	DWORD serial, namelen, flags;
	char fsname[100];
	struct mntent *mnt;
	wchar_t wfile_buf[PATH_MAX];
	wcs_result wr_file;
	wchar_t wfsname[100];
	/* Valid filesystem names don't seem to be documented.  The following
	 * are present in Wine (dlls/kernel32/volume.c). */
#define FS_NAMES "NTFS\0FAT\0FAT32\0CDFS\0UDF\0"
	int fstypes[] = {0, 0x5346544e, 0x4006, 0x4006, 0x9660, 0x15013346};

	if ( (mnt=find_mount_point(file, 0)) == NULL ) {
		return -1;
	}

	file = mnt->mnt_dir;
	wr_file = bb_to_wcs(file, wfile_buf, sizeof(wfile_buf));

	if ( !GetDiskFreeSpaceExW(wr_file.str, (PULARGE_INTEGER) &free_bytes_available,
			(PULARGE_INTEGER) &total_number_of_bytes,
			(PULARGE_INTEGER) &total_number_of_free_bytes) ) {
		wcs_free(&wr_file);
		errno = err_win_to_posix();
		return -1;
	}

	if ( !GetVolumeInformationW(wr_file.str, NULL, 0, &serial, &namelen, &flags,
								wfsname, 100) ) {
		wcs_free(&wr_file);
		errno = err_win_to_posix();
		return -1;
	}

	wcs_free(&wr_file);

	{
		mbs_result mr = bb_to_mbs(wfsname, fsname, sizeof(fsname));
		if (mr.str != fsname) {
			strncpy(fsname, mr.str, sizeof(fsname));
			fsname[sizeof(fsname) - 1] = '\0';
			mbs_free(&mr);
		}
	}

	memset(buf, 0, sizeof(*buf));

	/* XXX I couldn't determine how to get block size.  MSDN has a
	 * unhelpful hard-coded list here:
	 * http://support.microsoft.com/kb/140365
	 * but this depends on the filesystem type, the size of the disk and
	 * the version of Windows.  So this code assumes the disk is NTFS
	 * and the version of Windows is >= Win2K.
	 */
	if (total_number_of_bytes < UINT64_C(16) * 1024 * 1024 * 1024 * 1024)
		buf->f_bsize = 4096;
	else if (total_number_of_bytes < UINT64_C(32) * 1024 * 1024 * 1024 * 1024)
		buf->f_bsize = 8192;
	else if (total_number_of_bytes < UINT64_C(64) * 1024 * 1024 * 1024 * 1024)
		buf->f_bsize = 16384;
	else if (total_number_of_bytes < UINT64_C(128) * 1024 * 1024 * 1024 * 1024)
		buf->f_bsize = 32768;
	else
		buf->f_bsize = 65536;

	buf->f_type = fstypes[index_in_strings(FS_NAMES, fsname)+1];
	buf->f_frsize = buf->f_bsize;
	buf->f_blocks = total_number_of_bytes / buf->f_bsize;
	buf->f_bfree = total_number_of_free_bytes / buf->f_bsize;
	buf->f_bavail = free_bytes_available / buf->f_bsize;
	//buf->f_files = 0;
	//buf->f_ffree = 0;
	buf->f_fsid = serial;
	//buf->f_flag = 0;
	buf->f_namelen = namelen;

	return 0;
}

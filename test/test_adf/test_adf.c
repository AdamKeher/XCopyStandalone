/*
   ADFlib and the streaming directory walk, on a host.

   Everything here runs against the real vendored library in src/adflib/ and the
   real XCopyAdfWalk.c, not copies. What stands in for the SD card is upstream's
   dump driver - the same AdfDeviceDriver vtable over stdio that XCopyAdfSdDriver
   implements over SdFat - so what gets checked is the contract both of them have
   to satisfy.

   What is worth testing here is what cannot be checked by looking at the device:
   whether a written image is actually correct. On hardware the only way to know is
   to put the disk in an Amiga. Here it is an assertion.

   Deliberately not here: the SD and floppy drivers themselves, which need SdFat and
   a drive. Those are proven by running the firmware.
*/

#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adflib.h"
#include "adf_dev_driver_dump.h"
#include "XCopyAdfWalk.h"

/* An 880K DD floppy: what every Amiga ADF is. */
#define DD_CYLINDERS 80
#define DD_HEADS 2
#define DD_SECTORS 11
#define DD_BLOCKS (DD_CYLINDERS * DD_HEADS * DD_SECTORS)

static const char *const IMAGE = "test_scratch.adf";

/* WHAT THE WALK REPORTS */

#define MAX_SEEN 32

struct Seen
{
    char name[MAX_SEEN][32];
    int type[MAX_SEEN];
    uint32_t size[MAX_SEEN];
    int count;
    int faults;
};

static void seeEntry(void *context, const struct AdfEntry *entry)
{
    struct Seen *seen = (struct Seen *)context;
    if (seen->count >= MAX_SEEN)
        return;
    snprintf(seen->name[seen->count], sizeof(seen->name[0]), "%s",
             entry->name ? entry->name : "");
    seen->type[seen->count] = entry->type;
    seen->size[seen->count] = entry->size;
    seen->count++;
}

static void seeFault(void *context, ADF_SECTNUM sector, AdfWalkFault fault)
{
    (void)sector;
    (void)fault;
    ((struct Seen *)context)->faults++;
}

static void walk(struct AdfVolume *vol, ADF_SECTNUM dir, struct Seen *seen)
{
    struct AdfEntryBlock scratch[2];
    memset(seen, 0, sizeof(*seen));
    TEST_ASSERT_TRUE(adfWalkDir(vol, dir, scratch, seeEntry, seeFault, seen));
}

static int indexOf(const struct Seen *seen, const char *name)
{
    for (int i = 0; i < seen->count; i++)
        if (strcmp(seen->name[i], name) == 0)
            return i;
    return -1;
}

/* HELPERS

   The create / format / mount sequence, in one place, because ADFlib's is not
   obvious and getting it wrong fails in ways that look like something else.

   Two things to know. adfVolCreate()'s start and len are in CYLINDERS, not blocks -
   it multiplies by heads * sectors itself - and the AdfVolume it returns is a
   formatting result rather than a mount: its bitmap has already been freed, its
   datablockSize was never set, and it is not in dev->volList, so nothing else will
   ever free it. Using it as though it were mounted is what the first draft of this
   file did, and every write failed with a block number off the end of the device.
*/

static void discardImage(void) { remove(IMAGE); }

//! Create and format a fresh image, leaving nothing open.
static void formatImage(const char *volName, uint8_t volType)
{
    discardImage();

    struct AdfDevice *dev = adfDevCreate("dump", IMAGE,
                                         DD_CYLINDERS, DD_HEADS, DD_SECTORS);
    TEST_ASSERT_NOT_NULL(dev);

    struct AdfVolume *vol = adfVolCreate(dev, 0, DD_CYLINDERS, volName, volType);
    TEST_ASSERT_NOT_NULL(vol);

    /* Not adfDevUnMount()'s to free - it only frees what is in volList. */
    adfVolUnMount(vol);
    free(vol->volName);
    free(vol);

    adfDevClose(dev);
}

//! Open an already formatted image and mount its first volume.
static struct AdfDevice *openImage(AdfAccessMode mode, struct AdfVolume **volOut)
{
    struct AdfDevice *dev = adfDevOpen(IMAGE, mode);
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ASSERT_EQUAL_INT(ADF_RC_OK, adfDevMount(dev));
    TEST_ASSERT_EQUAL_INT(1, dev->nVol);

    struct AdfVolume *vol = adfVolMount(dev, 0, mode);
    TEST_ASSERT_NOT_NULL(vol);

    *volOut = vol;
    return dev;
}

//! formatImage() then openImage() read-write: the common case.
static struct AdfDevice *freshImage(const char *volName,
                                    uint8_t volType,
                                    struct AdfVolume **volOut)
{
    formatImage(volName, volType);
    return openImage(ADF_ACCESS_MODE_READWRITE, volOut);
}

static void closeImage(struct AdfDevice *dev, struct AdfVolume *vol)
{
    if (vol != NULL)
        adfVolUnMount(vol);
    if (dev != NULL)
        adfDevClose(dev); /* which adfDevUnMount()s, freeing volList */
}

static void writeFile(struct AdfVolume *vol,
                      const char *name,
                      const uint8_t *data,
                      uint32_t length)
{
    struct AdfFile *file = adfFileOpen(vol, name, ADF_FILE_MODE_WRITE);
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_UINT32(length, adfFileWrite(file, length, data));
    adfFileClose(file);
}

/* TESTS */

static void test_create_and_reopen_detects_dd_geometry(void)
{
    struct AdfVolume *vol = NULL;
    struct AdfDevice *dev = freshImage("Test", ADF_DOSFS_FFS, &vol);
    closeImage(dev, vol);

    /*
       Reopening is the interesting half. The old integration hardcoded 80/2/11
       inside the driver whatever the file was, so this could not have failed and
       could not have caught an HD image being read as DD either. Now the geometry
       comes from the length of the file, worked out by the library.
    */
    dev = adfDevOpen(IMAGE, ADF_ACCESS_MODE_READONLY);
    TEST_ASSERT_NOT_NULL(dev);

    TEST_ASSERT_EQUAL_UINT32(DD_BLOCKS, dev->sizeBlocks);
    TEST_ASSERT_EQUAL_UINT32(DD_CYLINDERS, dev->geometry.cylinders);
    TEST_ASSERT_EQUAL_UINT32(DD_HEADS, dev->geometry.heads);
    TEST_ASSERT_EQUAL_UINT32(DD_SECTORS, dev->geometry.sectors);
    TEST_ASSERT_EQUAL_INT(ADF_DEVTYPE_FDD, dev->type);
    TEST_ASSERT_EQUAL_INT(ADF_DEVCLASS_FLOP, dev->class);

    adfDevClose(dev);
    discardImage();
}

static void test_volume_name_and_filesystem_survive_a_remount(void)
{
    struct AdfVolume *vol = NULL;
    struct AdfDevice *dev = freshImage("Workbench", ADF_DOSFS_FFS | ADF_DOSFS_INTL, &vol);
    closeImage(dev, vol);

    dev = openImage(ADF_ACCESS_MODE_READONLY, &vol);

    TEST_ASSERT_EQUAL_STRING("Workbench", vol->volName);
    TEST_ASSERT_TRUE(adfVolIsFFS(vol));
    TEST_ASSERT_TRUE(adfVolHasINTL(vol));
    TEST_ASSERT_EQUAL_UINT(512, vol->datablockSize);

    closeImage(dev, vol);
    discardImage();
}

static void test_ofs_volume_uses_488_byte_data_blocks(void)
{
    struct AdfVolume *vol = NULL;
    struct AdfDevice *dev = freshImage("Old", ADF_DOSFS_OFS, &vol);

    TEST_ASSERT_FALSE(adfVolIsFFS(vol));
    TEST_ASSERT_EQUAL_UINT(488, vol->datablockSize);

    closeImage(dev, vol);
    discardImage();
}

static void test_file_written_reads_back_identical(void)
{
    /*
       Bigger than one data block and not a multiple of one, so the last block is
       partial and at least one extension block is involved. A 512 byte round trip
       would pass on code that mishandles both.
    */
    const uint32_t length = 5000;
    uint8_t *written = malloc(length);
    uint8_t *read = malloc(length);
    TEST_ASSERT_NOT_NULL(written);
    TEST_ASSERT_NOT_NULL(read);
    for (uint32_t i = 0; i < length; i++)
        written[i] = (uint8_t)(i * 7 + (i >> 8));

    struct AdfVolume *vol = NULL;
    struct AdfDevice *dev = freshImage("Test", ADF_DOSFS_FFS, &vol);
    writeFile(vol, "payload.dat", written, length);
    closeImage(dev, vol);

    /* Reopened rather than read back through the same handle: this is the test
       that the bytes reached the file, not that they reached a cache. */
    dev = openImage(ADF_ACCESS_MODE_READONLY, &vol);

    struct AdfFile *file = adfFileOpen(vol, "payload.dat", ADF_FILE_MODE_READ);
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_UINT32(length, adfFileGetSize(file));
    TEST_ASSERT_EQUAL_UINT32(length, adfFileRead(file, length, read));
    TEST_ASSERT_TRUE(adfFileAtEOF(file));
    adfFileClose(file);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(written, read, length);

    closeImage(dev, vol);
    discardImage();
    free(written);
    free(read);
}

static void test_ofs_file_written_reads_back_identical(void)
{
    /* OFS puts a 24 byte header in every data block, so the block walking is a
       different path from FFS and has its own way of going wrong. */
    const uint32_t length = 3000;
    uint8_t written[3000];
    uint8_t read[3000];
    for (uint32_t i = 0; i < length; i++)
        written[i] = (uint8_t)(255 - (i % 251));

    struct AdfVolume *vol = NULL;
    struct AdfDevice *dev = freshImage("Old", ADF_DOSFS_OFS, &vol);
    writeFile(vol, "payload.dat", written, length);
    closeImage(dev, vol);

    dev = openImage(ADF_ACCESS_MODE_READONLY, &vol);

    struct AdfFile *file = adfFileOpen(vol, "payload.dat", ADF_FILE_MODE_READ);
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_UINT32(length, adfFileRead(file, length, read));
    adfFileClose(file);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(written, read, length);

    closeImage(dev, vol);
    discardImage();
}

static void test_walk_reports_every_entry_once(void)
{
    struct AdfVolume *vol = NULL;
    struct AdfDevice *dev = freshImage("Test", ADF_DOSFS_FFS, &vol);

    static const uint8_t payload[100] = {0};
    writeFile(vol, "one.txt", payload, sizeof(payload));
    writeFile(vol, "two.txt", payload, 50);
    TEST_ASSERT_EQUAL_INT(ADF_RC_OK, adfCreateDir(vol, vol->rootBlock, "adir"));

    struct Seen seen;
    walk(vol, vol->rootBlock, &seen);

    TEST_ASSERT_EQUAL_INT(3, seen.count);
    TEST_ASSERT_EQUAL_INT(0, seen.faults);

    const int one = indexOf(&seen, "one.txt");
    const int two = indexOf(&seen, "two.txt");
    const int dir = indexOf(&seen, "adir");
    TEST_ASSERT_TRUE(one >= 0 && two >= 0 && dir >= 0);

    TEST_ASSERT_EQUAL_INT(ADF_ST_FILE, seen.type[one]);
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), seen.size[one]);
    TEST_ASSERT_EQUAL_UINT32(50, seen.size[two]);
    TEST_ASSERT_EQUAL_INT(ADF_ST_DIR, seen.type[dir]);

    closeImage(dev, vol);
    discardImage();
}

static void test_walk_follows_a_hash_chain(void)
{
    /*
       ADF_HT_SIZE is 72, so 40 entries in one directory is well past the point
       where names start sharing a hash bucket and the walk has to follow
       nextSameHash to find them. A walk that only read the hash table would
       report some subset of these and look plausible doing it.
    */
    struct AdfVolume *vol = NULL;
    struct AdfDevice *dev = freshImage("Test", ADF_DOSFS_FFS, &vol);

    static const uint8_t payload[8] = {0};
    const int expected = 40;
    for (int i = 0; i < expected; i++)
    {
        char name[16];
        snprintf(name, sizeof(name), "file%02d.txt", i);
        writeFile(vol, name, payload, sizeof(payload));
    }

    struct AdfEntryBlock scratch[2];
    TEST_ASSERT_EQUAL_INT(expected, adfWalkCount(vol, vol->rootBlock, scratch));

    closeImage(dev, vol);
    discardImage();
}

static void test_walk_descends_into_a_subdirectory(void)
{
    struct AdfVolume *vol = NULL;
    struct AdfDevice *dev = freshImage("Test", ADF_DOSFS_FFS, &vol);

    static const uint8_t payload[16] = {0};
    TEST_ASSERT_EQUAL_INT(ADF_RC_OK, adfCreateDir(vol, vol->rootBlock, "c"));
    TEST_ASSERT_EQUAL_INT(ADF_RC_OK, adfChangeDir(vol, "c"));
    writeFile(vol, "list", payload, sizeof(payload));
    writeFile(vol, "dir", payload, sizeof(payload));

    struct Seen seen;
    walk(vol, vol->curDirPtr, &seen);
    TEST_ASSERT_EQUAL_INT(2, seen.count);
    TEST_ASSERT_TRUE(indexOf(&seen, "list") >= 0);

    /* The root still holds only the directory: a walk of one level must not have
       reported what is inside it. */
    walk(vol, vol->rootBlock, &seen);
    TEST_ASSERT_EQUAL_INT(1, seen.count);
    TEST_ASSERT_EQUAL_STRING("c", seen.name[0]);

    closeImage(dev, vol);
    discardImage();
}

static void test_delete_frees_the_blocks_it_used(void)
{
    /*
       This is the local fix that had been carried in the 0.7.11a fork for years -
       adfRemoveEntry() not marking the file header block free - restated as a test
       against the upstream that now has it. A leak here is invisible until a disk
       fills up with files that are not there.
    */
    struct AdfVolume *vol = NULL;
    struct AdfDevice *dev = freshImage("Test", ADF_DOSFS_FFS, &vol);

    const uint32_t before = adfCountFreeBlocks(vol);

    uint8_t payload[2000];
    memset(payload, 0xA5, sizeof(payload));
    writeFile(vol, "temp.dat", payload, sizeof(payload));

    const uint32_t used = adfCountFreeBlocks(vol);
    TEST_ASSERT_TRUE(used < before);

    TEST_ASSERT_EQUAL_INT(ADF_RC_OK, adfRemoveEntry(vol, vol->rootBlock, "temp.dat"));
    TEST_ASSERT_EQUAL_UINT32(before, adfCountFreeBlocks(vol));

    struct AdfEntryBlock scratch[2];
    TEST_ASSERT_EQUAL_INT(0, adfWalkCount(vol, vol->rootBlock, scratch));

    closeImage(dev, vol);
    discardImage();
}

static void test_free_block_count_survives_a_remount(void)
{
    /* The bitmap is written back on unmount. If it were not, a disk would look
       empty again after every remount and later writes would overwrite files. */
    struct AdfVolume *vol = NULL;
    struct AdfDevice *dev = freshImage("Test", ADF_DOSFS_FFS, &vol);

    uint8_t payload[4096];
    memset(payload, 0x5A, sizeof(payload));
    writeFile(vol, "big.dat", payload, sizeof(payload));
    const uint32_t expected = adfCountFreeBlocks(vol);
    closeImage(dev, vol);

    dev = openImage(ADF_ACCESS_MODE_READONLY, &vol);

    TEST_ASSERT_EQUAL_UINT32(expected, adfCountFreeBlocks(vol));

    closeImage(dev, vol);
    discardImage();
}

static void test_read_only_volume_refuses_a_write(void)
{
    struct AdfVolume *vol = NULL;
    struct AdfDevice *dev = freshImage("Test", ADF_DOSFS_FFS, &vol);
    static const uint8_t payload[16] = {0};
    writeFile(vol, "kept.txt", payload, sizeof(payload));
    closeImage(dev, vol);

    dev = openImage(ADF_ACCESS_MODE_READONLY, &vol);
    TEST_ASSERT_TRUE(vol->readOnly);

    TEST_ASSERT_NULL(adfFileOpen(vol, "new.txt", ADF_FILE_MODE_WRITE));
    TEST_ASSERT_NOT_EQUAL(ADF_RC_OK, adfRemoveEntry(vol, vol->rootBlock, "kept.txt"));

    struct AdfEntryBlock scratch[2];
    TEST_ASSERT_EQUAL_INT(1, adfWalkCount(vol, vol->rootBlock, scratch));

    closeImage(dev, vol);
    discardImage();
}

static void test_walk_of_an_empty_directory_reports_nothing(void)
{
    struct AdfVolume *vol = NULL;
    struct AdfDevice *dev = freshImage("Empty", ADF_DOSFS_FFS, &vol);

    struct Seen seen;
    walk(vol, vol->rootBlock, &seen);
    TEST_ASSERT_EQUAL_INT(0, seen.count);
    TEST_ASSERT_EQUAL_INT(0, seen.faults);

    closeImage(dev, vol);
    discardImage();
}

static void test_opening_a_file_that_is_not_there_fails(void)
{
    struct AdfVolume *vol = NULL;
    struct AdfDevice *dev = freshImage("Test", ADF_DOSFS_FFS, &vol);

    TEST_ASSERT_NULL(adfFileOpen(vol, "absent.txt", ADF_FILE_MODE_READ));

    closeImage(dev, vol);
    discardImage();
}

static void test_opening_something_that_is_not_an_image_fails(void)
{
    /* What "dump <a text file>" does. It has to be a clean failure, not a mount of
       whatever the first 512 bytes happen to look like. */
    discardImage();
    FILE *f = fopen(IMAGE, "wb");
    TEST_ASSERT_NOT_NULL(f);
    for (int i = 0; i < 8; i++)
        fputs("this is not an amiga disk image at all, not even close\n", f);
    fclose(f);

    struct AdfDevice *dev = adfDevOpen(IMAGE, ADF_ACCESS_MODE_READONLY);
    if (dev != NULL)
    {
        /* Opening can succeed - it is only a file of some length - but mounting a
           filesystem out of it must not. */
        TEST_ASSERT_NOT_EQUAL(ADF_RC_OK, adfDevMount(dev));
        adfDevClose(dev);
    }

    discardImage();
}

void setUp(void) {}
void tearDown(void) { discardImage(); }

int main(void)
{
    /* adfLibInit() registers the dump and ramdisk drivers and runs the internal
       consistency checks. The firmware does not use it - XCopyAdfHost registers the
       drivers this board has and the checks are static_asserts - but here it is
       exactly what is wanted, dump driver included. */
    if (adfLibInit() != ADF_RC_OK)
    {
        fprintf(stderr, "adfLibInit failed: the library's internal checks did not pass\n");
        return 1;
    }

    /* Errors and warnings are expected in several of these and would otherwise
       scroll past the results looking like failures. */
    adfEnvSetProperty(ADF_PR_QUIET, true);

    UNITY_BEGIN();

    RUN_TEST(test_create_and_reopen_detects_dd_geometry);
    RUN_TEST(test_volume_name_and_filesystem_survive_a_remount);
    RUN_TEST(test_ofs_volume_uses_488_byte_data_blocks);

    RUN_TEST(test_file_written_reads_back_identical);
    RUN_TEST(test_ofs_file_written_reads_back_identical);

    RUN_TEST(test_walk_of_an_empty_directory_reports_nothing);
    RUN_TEST(test_walk_reports_every_entry_once);
    RUN_TEST(test_walk_follows_a_hash_chain);
    RUN_TEST(test_walk_descends_into_a_subdirectory);

    RUN_TEST(test_delete_frees_the_blocks_it_used);
    RUN_TEST(test_free_block_count_survives_a_remount);

    RUN_TEST(test_read_only_volume_refuses_a_write);
    RUN_TEST(test_opening_a_file_that_is_not_there_fails);
    RUN_TEST(test_opening_something_that_is_not_an_image_fails);

    const int failures = UNITY_END();
    adfLibCleanUp();
    return failures;
}

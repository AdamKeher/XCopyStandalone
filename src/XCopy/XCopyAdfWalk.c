#include "XCopyAdfWalk.h"

#include "../adflib/adf_blk.h"
#include "../adflib/adf_err.h"

#include <stdlib.h>

bool adfWalkDir(struct AdfVolume *vol,
                ADF_SECTNUM dirSector,
                struct AdfEntryBlock scratch[2],
                AdfWalkVisit visit,
                AdfWalkWarn warn,
                void *context)
{
    if (vol == NULL || visit == NULL || scratch == NULL)
        return false;

    struct AdfEntryBlock *const parent = &scratch[0];
    struct AdfEntryBlock *const current = &scratch[1];

    if (adfReadEntryBlock(vol, dirSector, parent) != ADF_RC_OK)
        return false;

    for (int i = 0; i < ADF_HT_SIZE; i++)
    {
        ADF_SECTNUM sector = parent->hashTable[i];

        while (sector != 0)
        {
            struct AdfEntry entry;
            if (adfEntryRead(vol, sector, &entry, current) != ADF_RC_OK)
                return false;

            visit(context, &entry);

            /* adfEntryRead() strdup()s the name and the comment into the entry
               it was given. Freeing them here is what keeps the peak at one entry
               rather than all of them, which is the whole point of walking.

               NOT adfFreeEntry(): that frees the struct as well, because the only
               caller upstream has is adfFreeDirList() walking a list of entries it
               malloc'd one at a time. Ours is on the stack. Passing it to
               adfFreeEntry() free()s a stack address, which on the host is an
               immediate crash and on the Teensy would be silent heap corruption
               surfacing somewhere else entirely. */
            free(entry.name);
            free(entry.comment);

            if (current->nextSameHash == 0)
                break;

            /* ADFlib upstream issue #99. Some disks - it reads as deliberate,
               a way of stopping people looking at the contents - link an entry's
               nextSameHash back to itself, or to another entry already in the
               parent's hash table. AmigaOS locks up on those and so would this
               loop. Both end the chain rather than the walk: everything already
               reported is real, and the rest of the hash table is usually fine. */
            if (sector == current->nextSameHash)
            {
                if (warn != NULL)
                    warn(context, sector, ADF_WALK_SELF_LINK);
                break;
            }

            bool loops = false;
            for (int j = 0; j < ADF_HT_SIZE; j++)
            {
                if (parent->hashTable[j] == current->nextSameHash)
                {
                    if (warn != NULL)
                        warn(context, sector, ADF_WALK_HASH_LOOP);
                    loops = true;
                    break;
                }
            }
            if (loops)
                break;

            sector = current->nextSameHash;
        }
    }

    return true;
}

static void countOne(void *context, const struct AdfEntry *entry)
{
    (void)entry;
    (*(int *)context)++;
}

int adfWalkCount(struct AdfVolume *vol,
                 ADF_SECTNUM dirSector,
                 struct AdfEntryBlock scratch[2])
{
    int count = 0;
    if (!adfWalkDir(vol, dirSector, scratch, countOne, NULL, &count))
        return -1;
    return count;
}

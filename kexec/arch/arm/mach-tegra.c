#include <stdint.h>
#include <stdio.h>
#include <libfdt.h>

#include "../../kexec.h"
#include "mach.h"

// The assumption is that most Tegra soc dtbs have nvidia,boardids and this will work in general.

#define INVALID_REV_ID 0xFFFFFFFF

struct tegra_id
{
    uint32_t platform_id;
    uint32_t hardware_id;
    uint32_t board_rev;
};

static uint32_t tegra_dtb_compatible(void *dtb, struct tegra_id *devid, struct tegra_id *dtb_id)
{
    int root_offset;
    const void *prop;
    int len;

    root_offset = fdt_path_offset(dtb, "/");
    if (root_offset < 0)
    {
        fprintf(stderr, "DTB: Couldn't find root path in dtb!\n");
        return 0;
    }

    prop = fdt_getprop(dtb, root_offset, "nvidia,boardids", &len);
    if (!prop || len <= 0) {
        printf("DTB: nvidia,boardids entry not found\n");
        return 0;
    } else if (len < (int)sizeof(struct tegra_id)) {
        printf("DTB: nvidia,boardids entry size mismatch (%d != %d)\n",
            len, sizeof(struct tegra_id));
        return 0;
    }

    dtb_id->platform_id = fdt32_to_cpu(((const struct tegra_id *)prop)->platform_id);
    dtb_id->hardware_id = fdt32_to_cpu(((const struct tegra_id *)prop)->hardware_id);
    dtb_id->board_rev = fdt32_to_cpu(((const struct tegra_id *)prop)->board_rev);

    if (dtb_id->platform_id != devid->platform_id ||
        dtb_id->hardware_id != devid->hardware_id) {
        return 0;
    }

    return 1;
}

static int tegra_choose_dtb(const char *dtb_img, off_t dtb_len, char **dtb_buf, off_t *dtb_length)
{
    char *dtb = (char*)dtb_img;
    char *dtb_end = dtb + dtb_len;
    char* My_String;
    FILE *f;
    struct tegra_id devid, dtb_id;
    char *bestmatch_tag = NULL;
    uint32_t bestmatch_tag_size;
    uint32_t bestmatch_board_rev_id = INVALID_REV_ID;

    f = fopen("/proc/device-tree/nvidia,boardids", "r");
    if(!f)
    {
        fprintf(stderr, "DTB: Couldn't open /proc/device-tree/nvidia,boardids!\n");
        return 0;
    }

    fread(&devid, sizeof(struct tegra_id), 1, f);
    fclose(f);

    devid.platform_id = fdt32_to_cpu(devid.platform_id);
    devid.hardware_id = fdt32_to_cpu(devid.hardware_id);
    devid.board_rev = fdt32_to_cpu(devid.board_rev);

    printf("Device Tree: platform %u hw %u board %u\n",
           devid.platform_id, devid.hardware_id, devid.board_rev);

    while(dtb + sizeof(struct fdt_header) < dtb_end)
    {
        uint32_t dtb_soc_rev_id;
        struct fdt_header dtb_hdr;
        uint32_t dtb_size;

        /* the DTB could be unaligned, so extract the header,
         * and operate on it separately */
        memcpy(&dtb_hdr, dtb, sizeof(struct fdt_header));
        if (fdt_check_header((const void *)&dtb_hdr) != 0 ||
            (dtb + fdt_totalsize((const void *)&dtb_hdr) > dtb_end))
        {
			fprintf(stderr, "DTB: Invalid dtb header! %u\n",fdt_check_header((const void *)&dtb_hdr));
            break;
        }
        dtb_size = fdt_totalsize(&dtb_hdr);

        if(tegra_dtb_compatible(dtb, &devid, &dtb_id))
        {
            if (dtb_id.board_rev == devid.board_rev)
            {
                *dtb_buf = xmalloc(dtb_size);
                memcpy(*dtb_buf, dtb, dtb_size);
                *dtb_length = dtb_size;
                printf("DTB: match %u, my id %u, len %u\n",
                       dtb_id.board_rev, devid.board_rev, dtb_size);
                return 1;
            }
            else if(dtb_id.board_rev < devid.board_rev)
            {
                if(bestmatch_board_rev_id == INVALID_REV_ID ||
		   bestmatch_board_rev_id < dtb_id.board_rev)
                {
                    bestmatch_tag = dtb;
                    bestmatch_tag_size = dtb_size;
                    bestmatch_board_rev_id = dtb_id.board_rev;
                }
            }
        }

        /* goto the next device tree if any */
        dtb += dtb_size;

        // try to skip padding in standalone dtb.img files
        while(dtb < dtb_end && *dtb == 0)
            ++dtb;
    }

    if(bestmatch_tag) {
        printf("DTB: bestmatch %u, my id %u\n",
                bestmatch_board_rev_id, devid.board_rev);
        *dtb_buf = xmalloc(bestmatch_tag_size);
        memcpy(*dtb_buf, bestmatch_tag, bestmatch_tag_size);
        *dtb_length = bestmatch_tag_size;
        return 1;
    }

    return 0;
}

static int tegra_add_extra_regs(void *dtb_buf)
{
    FILE *f;
    uint32_t reg;
    int res;
    int off;

    off = fdt_path_offset(dtb_buf, "/memory");
    if (off < 0)
    {
        fprintf(stderr, "DTB: Could not find memory node.\n");
        return -1;
    }

    f = fopen("/proc/device-tree/memory@0x80000000/reg", "r");
    if(!f)
    {
        fprintf(stderr, "DTB: Failed to open /proc/device-tree/memory@0x80000000/reg!\n");
        return -1;
    }

    fdt_delprop(dtb_buf, off, "reg");

    while(fread(&reg, sizeof(reg), 1, f) == 1)
        fdt_appendprop(dtb_buf, off, "reg", &reg, sizeof(reg));

    fclose(f);
    return 0;
}

const struct arm_mach arm_mach_tegra = {
    .boardnames = { "mocha", "tn8", NULL },
    .choose_dtb = tegra_choose_dtb,
    .add_extra_regs = tegra_add_extra_regs
};

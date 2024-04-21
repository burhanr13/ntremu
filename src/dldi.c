#include "dldi.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "emulator_state.h"

struct {
    u32 secnum;
    u32 secbuf[SECTOR_SIZE >> 2];
    int i;
} dldi;

void dldi_patch_binary(u8* b, u32 len) {
    if (ntremu.dldi_sd_fd < 0) return;
    FILE* driver = fopen("dldi_driver/ntremu_driver.dldi", "rb");
    if (!driver) return;
    for (int i = 0; i < len; i += 0x40) {
        DLDIHeader* hdr = (DLDIHeader*) &b[i];
        if (hdr->id == DLDI_ID &&
            !memcmp(hdr->magic, DLDI_MAGIC, sizeof DLDI_MAGIC)) {
            u32 space = hdr->space_avail;
            u32 old_text_start = hdr->text_start;
            fread(hdr, 1, 1 << space, driver);
            hdr->space_avail = space;
            s32 adj = old_text_start - hdr->text_start;
            hdr->text_start += adj;
            hdr->data_end += adj;
            hdr->glue_start += adj;
            hdr->glue_end += adj;
            hdr->got_start += adj;
            hdr->got_end += adj;
            hdr->bss_start += adj;
            hdr->bss_end += adj;
            hdr->startup += adj;
            hdr->isInserted += adj;
            hdr->readSectors += adj;
            hdr->writeSectors += adj;
            hdr->clearStatus += adj;
            break;
        }
    }
    fclose(driver);
}

u32 dldi_get_status() {
    if (ntremu.dldi_sd_fd < 0 ||
        dldi.secnum >= ntremu.dldi_sd_size / SECTOR_SIZE) {
        dldi.secnum = 0;
        return 0;
    }
    return 1;
}

void dldi_write_addr(u32 addr) {
    if (ntremu.dldi_sd_fd < 0) return;
    dldi.secnum = addr;
    dldi.i = 0;
    if (dldi.secnum < ntremu.dldi_sd_size / SECTOR_SIZE) {
        lseek(ntremu.dldi_sd_fd, dldi.secnum * SECTOR_SIZE, SEEK_SET);
    }
}

void dldi_write_data(u32 data) {
    if (ntremu.dldi_sd_fd < 0) return;
    dldi.secbuf[dldi.i++] = data;
    if (dldi.i == SECTOR_SIZE / 4) {
        dldi.i = 0;
        write(ntremu.dldi_sd_fd, dldi.secbuf, SECTOR_SIZE);
    }
}

u32 dldi_read_data() {
    if (ntremu.dldi_sd_fd < 0) return -1;
    if (dldi.i == 0) {
        read(ntremu.dldi_sd_fd, dldi.secbuf, SECTOR_SIZE);
    }
    u32 a = dldi.secbuf[dldi.i++];
    if (dldi.i == SECTOR_SIZE / 4) dldi.i = 0;
    return a;
}
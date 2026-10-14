# RM-609 firmwares

- `rm-609-111.140.0058/` stock belle refresh 111.140.0058
- `rm-609-archive/RM-609.rar` stock archive
- `cfw-rm609/` cfw `Classic_Belle_Refresh_E6_RM-609.rar` + unzipped

unpacked core images are in work/

The BCM2727B1 VideoCore boot images (loader A/B and the main image) are part
of the kernel ROM (`unsigned_2727b1.dll`). `tools/vc_extract.py kern.bin outdir`
extracts them. `linux/kernel/build-vc.sh` does this at build time, so they are
never committed.
# PlatformIO helper for the doll envs: "Upload Filesystem Image" packs data_doll/ (the spoken
# phrases, about 360 kB) instead of data/ (books and clips, too big for the doll's 1.2 MB flash).
Import("env")  # noqa: F821 (PlatformIO provides it)
env.Replace(PROJECT_DATA_DIR=env.subst("$PROJECT_DIR/data_doll"))  # noqa: F821

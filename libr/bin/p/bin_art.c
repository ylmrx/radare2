/* radare - LGPL - Copyright 2015-2017 - pancake */

#include <r_types.h>
#include <r_util.h>
#include <r_lib.h>
#include <r_bin.h>

#ifdef _MSC_VER
typedef struct art_header_t {
#else
typedef struct __packed art_header_t {
#endif
	ut8 magic[4];
	ut8 version[4];
	ut32 image_base;
	ut32 image_size;
	ut32 bitmap_offset;
	ut32 bitmap_size;
	ut32 checksum; /* adler32 */
	ut32 oat_file_begin; // oat_file_begin
	ut32 oat_data_begin;
	ut32 oat_data_end;
	ut32 oat_file_end;
	/* patch_delta is the amount of the base address the image is relocated */
	st32 patch_delta;
	/* image_roots: address of an array of objects needed to initialize */
	ut32 image_roots;
	ut32 compile_pic;
} ARTHeader;

typedef struct {
	Sdb *kv;
	ARTHeader art;
} ArtObj;

static int art_header_load(ARTHeader *art, RBuffer *buf, Sdb *db) {
	/* TODO: handle read errors here */
	if (r_buf_size (buf) < sizeof (ARTHeader)) {
		return false;
	}
	(void) r_buf_fread_at (buf, 0, (ut8 *) art, "IIiiiiiiiiiiii", 1);
	sdb_set (db, "img.base", sdb_fmt ("0x%x", art->image_base), 0);
	sdb_set (db, "img.size", sdb_fmt ("0x%x", art->image_size), 0);
	sdb_set (db, "art.checksum", sdb_fmt ("0x%x", art->checksum), 0);
	sdb_set (db, "art.version", sdb_fmt ("%c%c%c",
			art->version[0], art->version[1], art->version[2]), 0);
	sdb_set (db, "oat.begin", sdb_fmt ("0x%x", art->oat_file_begin), 0);
	sdb_set (db, "oat.end", sdb_fmt ("0x%x", art->oat_file_end), 0);
	sdb_set (db, "oat_data.begin", sdb_fmt ("0x%x", art->oat_data_begin), 0);
	sdb_set (db, "oat_data.end", sdb_fmt ("0x%x", art->oat_data_end), 0);
	sdb_set (db, "patch_delta", sdb_fmt ("0x%x", art->patch_delta), 0);
	sdb_set (db, "image_roots", sdb_fmt ("0x%x", art->image_roots), 0);
	sdb_set (db, "compile_pic", sdb_fmt ("0x%x", art->compile_pic), 0);
	return true;
}

static Sdb *get_sdb(RBinFile *bf) {
	RBinObject *o = bf->o;
	if (!o) {
		return NULL;
	}
	ArtObj *ao = o->bin_obj;
	return ao? ao->kv: NULL;
}

static void *load_bytes(RBinFile *bf, const ut8 *buf, ut64 sz, ut64 la, Sdb *sdb){
	ArtObj *ao = R_NEW0 (ArtObj);
	if (!ao) {
		return NULL;
	}
	ao->kv = sdb_new0 ();
	if (!ao->kv) {
		free (ao);
		return NULL;
	}
	art_header_load (&ao->art, bf->buf, ao->kv);
	sdb_ns_set (sdb, "info", ao->kv);
	return ao;
}

static bool load(RBinFile *bf) {
	return true;
}

static int destroy(RBinFile *bf) {
	return true;
}

static ut64 baddr(RBinFile *bf) {
	ArtObj *ao = bf->o->bin_obj;
	return ao? ao->art.image_base: 0;
}

static RList *strings(RBinFile *bf) {
	return NULL;
}

static RBinInfo *info(RBinFile *bf) {
	ArtObj *ao;
	RBinInfo *ret;
	if (!bf || !bf->o || !bf->o->bin_obj) {
		return NULL;
	}
	ret = R_NEW0 (RBinInfo);
	if (!ret) {
		return NULL;
	}
	// art_header_load (&art, bf->buf);
	ao = bf->o->bin_obj;
	ret->lang = NULL;
	ret->file = bf->file? strdup (bf->file): NULL;
	ret->type = strdup ("ART");

	ret->bclass = malloc (5);
	memcpy (ret->bclass, &ao->art.version, 4);
	ret->bclass[3] = 0;

	ret->rclass = strdup ("program");
	ret->os = strdup ("android");
	ret->subsystem = strdup ("unknown");
	ret->machine = strdup ("arm");
	ret->arch = strdup ("arm");
	ret->has_va = 1;
	ret->has_lit = true;
	ret->has_pi = ao->art.compile_pic;
	ret->bits = 16; // 32? 64?
	ret->big_endian = 0;
	ret->dbg_info = 0;
	return ret;
}

static bool check_bytes(const ut8 *buf, ut64 length) {
	return (buf && length > 3 && !strncmp ((const char *) buf, "art\n", 4));
}

static RList *entries(RBinFile *bf) {
	RList *ret;
	RBinAddr *ptr = NULL;

	if (!(ret = r_list_new ())) {
		return NULL;
	}
	ret->free = free;
	if (!(ptr = R_NEW0 (RBinAddr))) {
		return ret;
	}
	ptr->paddr = ptr->vaddr = 0;
	r_list_append (ret, ptr);
	return ret;
}

static RList *sections(RBinFile *bf) {
	ArtObj *ao = bf->o->bin_obj;
	if (!ao) {
		return NULL;
	}
	ARTHeader art = ao->art;
	RList *ret = NULL;
	RBinSection *ptr = NULL;

	if (!(ret = r_list_new ())) {
		return NULL;
	}
	ret->free = free;

	if (!(ptr = R_NEW0 (RBinSection))) {
		return ret;
	}
	strncpy (ptr->name, "load", R_BIN_SIZEOF_STRINGS);
	ptr->size = bf->buf->length;
	ptr->vsize = art.image_size; // TODO: align?
	ptr->paddr = 0;
	ptr->vaddr = art.image_base;
	ptr->srwx = R_BIN_SCN_READABLE; // r--
	ptr->add = true;
	r_list_append (ret, ptr);

	if (!(ptr = R_NEW0 (RBinSection))) {
		return ret;
	}
	strncpy (ptr->name, "bitmap", R_BIN_SIZEOF_STRINGS);
	ptr->size = art.bitmap_size;
	ptr->vsize = art.bitmap_size;
	ptr->paddr = art.bitmap_offset;
	ptr->vaddr = art.image_base + art.bitmap_offset;
	ptr->srwx = R_BIN_SCN_READABLE | R_BIN_SCN_EXECUTABLE; // r-x
	ptr->add = true;
	r_list_append (ret, ptr);

	if (!(ptr = R_NEW0 (RBinSection))) {
		return ret;
	}
	strncpy (ptr->name, "oat", R_BIN_SIZEOF_STRINGS);
	ptr->paddr = art.bitmap_offset;
	ptr->vaddr = art.oat_file_begin;
	ptr->size = art.oat_file_end - art.oat_file_begin;
	ptr->vsize = ptr->size;
	ptr->srwx = R_BIN_SCN_READABLE | R_BIN_SCN_EXECUTABLE; // r-x
	ptr->add = true;
	r_list_append (ret, ptr);

	if (!(ptr = R_NEW0 (RBinSection))) {
		return ret;
	}
	strncpy (ptr->name, "oat_data", R_BIN_SIZEOF_STRINGS);
	ptr->paddr = art.bitmap_offset;
	ptr->vaddr = art.oat_data_begin;
	ptr->size = art.oat_data_end - art.oat_data_begin;
	ptr->vsize = ptr->size;
	ptr->srwx = R_BIN_SCN_READABLE; // r--
	ptr->add = true;
	r_list_append (ret, ptr);

	return ret;
}

RBinPlugin r_bin_plugin_art = {
	.name = "art",
	.desc = "Android Runtime",
	.license = "LGPL3",
	.get_sdb = &get_sdb,
	.load = &load,
	.load_bytes = &load_bytes,
	.destroy = &destroy,
	.check_bytes = &check_bytes,
	.baddr = &baddr,
	.sections = &sections,
	.entries = entries,
	.strings = &strings,
	.info = &info,
};

#ifndef CORELIB
RLibStruct radare_plugin = {
	.type = R_LIB_TYPE_BIN,
	.data = &r_bin_plugin_art,
	.version = R2_VERSION
};
#endif

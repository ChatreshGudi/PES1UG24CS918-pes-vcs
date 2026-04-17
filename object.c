// object.c — Content-addressable object store
//
// Every piece of data (file contents, directory listings, commits) is stored
// as an "object" named by its SHA-256 hash. Objects are stored under
// .pes/objects/XX/YYYYYY... where XX is the first two hex characters of the
// hash (directory sharding).
//
// PROVIDED functions: compute_hash, object_path, object_exists, hash_to_hex, hex_to_hash
// TODO functions:     object_write, object_read

#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    unsigned int hash_len;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, id_out->hash, &hash_len);
    EVP_MD_CTX_free(ctx);
}

void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// ─── object_write ────────────────────────────────────────────────────────────
// Stores data atomically via temp-file + rename to guarantee crash-safety.
// Uses fsync() on both the file and its parent directory.

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    const char *type_str = "";
    if (type == OBJ_BLOB)        type_str = "blob";
    else if (type == OBJ_TREE)   type_str = "tree";
    else if (type == OBJ_COMMIT) type_str = "commit";
    else return -1;

    char header[64];
    int header_len = snprintf(header, sizeof(header), "%s %zu", type_str, len);
    if (header_len < 0 || header_len >= (int)sizeof(header)) return -1;
    header_len += 1;

    size_t full_len = header_len + len;
    void *full_data = malloc(full_len);
    if (!full_data) return -1;
    memcpy(full_data, header, header_len);
    if (data && len > 0)
        memcpy((char *)full_data + header_len, data, len);

    compute_hash(full_data, full_len, id_out);

    if (object_exists(id_out)) {
        free(full_data);
        return 0;
    }

    // Determine final path and shard directory
    char path[512];
    object_path(id_out, path, sizeof(path));

    char shard_dir[512];
    snprintf(shard_dir, sizeof(shard_dir), "%s", path);
    char *last_slash = strrchr(shard_dir, '/');
    if (last_slash) *last_slash = '\0';
    mkdir(shard_dir, 0755);

    // Write to a temp file in the shard dir, then atomically rename
    char tmp_path[1024];
    snprintf(tmp_path, sizeof(tmp_path), "%s/tmp_XXXXXX", shard_dir);
    int tmp_fd = mkstemp(tmp_path);
    if (tmp_fd < 0) { free(full_data); return -1; }

    ssize_t written = write(tmp_fd, full_data, full_len);
    if (written != (ssize_t)full_len) {
        close(tmp_fd); unlink(tmp_path); free(full_data); return -1;
    }

    fsync(tmp_fd);
    close(tmp_fd);

    if (rename(tmp_path, path) < 0) {
        unlink(tmp_path); free(full_data); return -1;
    }

    // fsync the shard directory to persist the rename
    int dir_fd = open(shard_dir, O_RDONLY);
    if (dir_fd >= 0) { fsync(dir_fd); close(dir_fd); }

    free(full_data);
    return 0;
}

// object_read — TODO
int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    (void)id; (void)type_out; (void)data_out; (void)len_out;
    return -1;
}

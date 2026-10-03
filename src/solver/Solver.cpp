//
// Created by Xuefeng Huang on 2020/1/31.
//

#include "include/solver/Solver.h"

Solver::Solver() {

}

Solver::Solver(shared_ptr<GameTree> tree) {
    this->tree = tree;
}

shared_ptr<GameTree> Solver::getTree() {
    return this->tree;
}

#include <zlib.h>
#include <stdexcept>

bool Solver::read_config_from_file(const string& filepath, string& config_str, string& metadata_str) {
    gzFile file = gzopen(filepath.c_str(), "rb");
    if (!file) return false;
    
    char magic[4];
    if (gzread(file, magic, 4) != 4) {
        gzclose(file);
        return false;
    }
    if (magic[0] != 'T' || magic[1] != 'X' || magic[2] != 'S' || magic[3] != 'B') {
        gzclose(file);
        return false;
    }
    
    uint32_t version = 0;
    if (gzread(file, &version, 4) != 4) {
        gzclose(file);
        return false;
    }
    
    uint32_t config_len = 0;
    if (gzread(file, &config_len, 4) != 4) {
        gzclose(file);
        return false;
    }
    
    config_str.resize(config_len);
    if (config_len > 0) {
        if (gzread(file, &config_str[0], config_len) != (int)config_len) {
            gzclose(file);
            return false;
        }
    }
    
    uint32_t metadata_len = 0;
    if (gzread(file, &metadata_len, 4) != 4) {
        gzclose(file);
        return false;
    }
    
    metadata_str.resize(metadata_len);
    if (metadata_len > 0) {
        if (gzread(file, &metadata_str[0], metadata_len) != (int)metadata_len) {
            gzclose(file);
            return false;
        }
    }
    
    gzclose(file);
    return true;
}

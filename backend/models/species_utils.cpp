#include "species_utils.h"

#include <stdexcept>

SpeciesType species_type_from_name(const std::string& name) {
    if (name == "grass") {
        return SpeciesType::GRASS;
    }
    if (name == "cow") {
        return SpeciesType::COW;
    }
    if (name == "tiger") {
        return SpeciesType::TIGER;
    }
    throw std::invalid_argument("Unknown species name: " + name);
}

std::string name_from_species_type(SpeciesType type) {
    switch (type) {
        case SpeciesType::GRASS:
            return "grass";
        case SpeciesType::COW:
            return "cow";
        case SpeciesType::TIGER:
            return "tiger";
        default:
            return "";
    }
}

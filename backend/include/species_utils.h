#pragma once

#include <string>

#include "species.h"

SpeciesType species_type_from_name(const std::string& name);
std::string name_from_species_type(SpeciesType type);

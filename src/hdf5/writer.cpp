//
// Implementation of Writer class methods
//
// ICRAR - International Centre for Radio Astronomy Research
// (c) UWA - The University of Western Australia, 2017
// Copyright by UWA (in the framework of the ICRAR)
// All rights reserved
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston,
// MA 02111-1307  USA
//

#include <sstream>
#include <utility>

#include "hdf5/writer.h"
#include "exceptions.h"
#include "logging.h"


namespace shark {

namespace hdf5 {

const std::string Writer::NO_COMMENT;

Writer::Writer(const std::string &filename, bool overwrite,
	naming_convention group_naming_convention, naming_convention dataset_naming_convention,
	naming_convention attr_naming_convention) :
	IOBase(filename, overwrite ? H5F_ACC_TRUNC : H5F_ACC_EXCL),
	group_naming_convention(group_naming_convention),
	dataset_naming_convention(dataset_naming_convention),
	attr_naming_convention(attr_naming_convention)
{
}

static
void _check_entity_name(const std::string &name, const char *entity_type, naming_convention convention)
{
	if (!follows_convention(name, convention)) {
		std::ostringstream os;
		os << entity_type << " name " << name << " does not follow the " << convention << " naming convention";
		throw invalid_argument(os.str());
	}
}

void Writer::check_group_name(const std::string &group_name) const
{
	_check_entity_name(group_name, "Group", group_naming_convention);
}

void Writer::check_dataset_name(const std::string &dataset_name) const
{
	_check_entity_name(dataset_name, "Dataset", dataset_naming_convention);
}

void Writer::check_attr_name(const std::string &attr_name) const
{
	_check_entity_name(attr_name, "Attribute", attr_naming_convention);
}


#ifdef HDF5_NEWER_THAN_1_10_0
#define HDF5_FILE_GROUP_COMMON_BASE H5::Group
#else
#define HDF5_FILE_GROUP_COMMON_BASE H5::CommonFG
#endif

template <H5G_obj_t E>
inline static
typename entity_traits<E>::rettype
get_entity(const HDF5_FILE_GROUP_COMMON_BASE &file_or_group, const std::string &name);

template <> inline
H5::Group get_entity<H5G_GROUP>(const HDF5_FILE_GROUP_COMMON_BASE &file_or_group, const std::string &name)
{
	return file_or_group.openGroup(name);
}

template <> inline
H5::DataSet get_entity<H5G_DATASET>(const HDF5_FILE_GROUP_COMMON_BASE &file_or_group, const std::string &name)
{
	return file_or_group.openDataSet(name);
}

template <H5G_obj_t E, typename ... Ts>
inline static
typename entity_traits<E>::rettype
create_entity(const HDF5_FILE_GROUP_COMMON_BASE &file_or_group, const std::string &name, Ts&&...create_args);

template <> inline
H5::Group create_entity<H5G_GROUP>(const HDF5_FILE_GROUP_COMMON_BASE &file_or_group, const std::string &name)
{
	return file_or_group.createGroup(name);
}

template <> inline
H5::DataSet create_entity<H5G_DATASET>(const HDF5_FILE_GROUP_COMMON_BASE &file_or_group, const std::string &name, const H5::DataType &dataType, const H5::DataSpace &dataSpace)
{
	return file_or_group.createDataSet(name, dataType, dataSpace);
}

template <H5G_obj_t E, typename ... Ts>
typename entity_traits<E>::rettype
ensure_entity(const HDF5_FILE_GROUP_COMMON_BASE &file_or_group, const std::string &name, Ts&&...create_args)
{
	// Loop through subobjects and find entity
	for(hsize_t i = 0; i < file_or_group.getNumObjs(); i++) {

		auto objtype = file_or_group.getObjTypeByIdx(i);
		auto objname = file_or_group.getObjnameByIdx(i);

		// Name already used, sorry!
		if (name == objname && objtype != E) {
			std::ostringstream os;
			os << "Name " << name << " is already used by an object";
			throw object_exists(os.str());
		}

		// entity exists!
		else if (name == objname && objtype == E) {
			return get_entity<E>(file_or_group, name);
		}
	}

	// Nothing found, create new entity
	return create_entity<E>(file_or_group, name, std::forward<Ts>(create_args)...);
}

H5::Group Writer::ensure_group(const std::vector<std::string> &path) const
{
	if (path.size() == 1) {
		check_group_name(path[0]);
		return ensure_entity<H5G_GROUP>(hdf5_file, path[0]);
	}

	H5::Group group =  ensure_entity<H5G_GROUP>(hdf5_file, path.front());
	std::vector<std::string> group_paths(path.begin() + 1, path.end());
	for(auto &part: group_paths) {
		group = ensure_entity<H5G_GROUP>(group, part);
		check_group_name(part);
	}
	return group;
}

H5::DataSet Writer::ensure_dataset(const std::vector<std::string> &path, const H5::DataType &dataType, const H5::DataSpace &dataSpace) const
{
	if (path.size() == 1) {
		check_dataset_name(path[0]);
		return ensure_entity<H5G_DATASET>(hdf5_file, path[0], dataType, dataSpace);
	}

	std::vector<std::string> group_paths(path.begin(), path.end() - 1);
	auto &dataset_name = path.back();
	check_dataset_name(dataset_name);
	H5::Group group = ensure_group(group_paths);
	return ensure_entity<H5G_DATASET>(group, dataset_name, dataType, dataSpace);
}

}  // namespace hdf5

}  // namespace shark
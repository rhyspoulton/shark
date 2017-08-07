#include <memory>
#include <vector>

#include "exceptions.h"
#include "logging.h"
#include "tree_builder.h"


namespace shark {

TreeBuilder::TreeBuilder(ExecutionParameters exec_params) :
	exec_params(exec_params)
{
	// no-op
}

std::vector<std::shared_ptr<MergerTree>> TreeBuilder::build_trees(std::vector<std::shared_ptr<Halo>> halos)
{

	std::set<int> halo_snapshots;
	auto last_snapshot_to_consider = *std::end(exec_params.output_snapshots);

	// Find roots and create Trees for each of them
	std::vector<std::shared_ptr<MergerTree>> trees;
	for(const auto &halo: halos) {
		halo_snapshots.insert(halo->snapshot);
		if (halo->snapshot == last_snapshot_to_consider) {
			std::shared_ptr<MergerTree> tree = std::make_shared<MergerTree>();
			tree->add_halo(halo);
			halo->merger_tree = tree;
			trees.push_back(tree);
		}
	}

	// Index all halos by snapshot and by ID, we'll need them later
	std::map<int, std::vector<std::shared_ptr<Halo>>> halos_by_snapshot;
	std::map<Halo::id_t, std::shared_ptr<Halo>> halos_by_id;
	for(const auto &halo: halos) {
		halos_by_snapshot[halo->snapshot].push_back(halo);
		halos_by_id[halo->id] = halo;
	}

	// We do this by:
	//  1. Iterating over snapshots in descending order
	//  2. For each snapshot S we iterate over its halos
	//  3. For each halo H we iterate over its subhalos
	//  4. For each subhalo SH we find the halo with subhalo.descendant_halo_id
	//     (which we globally keep at the halos_by_id map)
	//  5. When the descendant halo DH is found, we find the particular subhalo
	//     DSH inside DH that matches SH's descendant_id
	//  6. Now we have SH, H, DSH and DH. We link them all together,
	//     and to their tree
	//
	//
	for(int snapshot: halo_snapshots) {
		for(const auto &halo: halos_by_snapshot[snapshot]) {
			for(const auto &subhalo: halo->all_subhalos()) {

				// if the descendant halo is not found, we don't consider this
				// halo anymore (and all its progenitors)
				auto it = halos_by_id.find(subhalo->descendant_halo_id);
				if (it == halos_by_id.end()) {
					LOG(info) << "Subhalo " << subhalo->id << " points to descendant halo "
					          << subhalo->descendant_halo_id << ", which doesn't exist. Ignoring "
					          << "this halo and the rest of its progenitors";
					halos_by_id.erase(halo->id);
					break;
				}

				auto &d_halo = halos_by_id[subhalo->descendant_halo_id];
				for(auto &d_subhalo: d_halo->all_subhalos()) {
					if (d_subhalo->id == subhalo->descendant_id) {
						link(subhalo, d_subhalo, halo, d_halo);
					}
				}
			}
		}

	}

	return trees;
}

void TreeBuilder::link(const std::shared_ptr<Subhalo> &subhalo, const std::shared_ptr<Subhalo> &d_subhalo,
                       const std::shared_ptr<Halo> &halo, const std::shared_ptr<Halo> &d_halo) {

	// Establish parentage link at subhalo level
	// Fail if subhalo has more than one descendant
	d_subhalo->ascendants.push_back(subhalo);
	if (subhalo->descendant) {
		throw invalid_data("invalid subhalo");
	}
	subhalo->descendant = d_subhalo;

	// Establish parentage link at halo level
	// Fail if a halo has more than one descendant
	// TODO: check that we're not adding "h" twice here
	d_halo->ascendants.push_back(halo);
	if (halo->descendant and halo->descendant->id != d_halo->id) {
		throw invalid_data("invalid halo");
	}
	halo->descendant = d_halo;

	// Link halo to merger tree and back
	halo->merger_tree = d_halo->merger_tree;
	halo->merger_tree->add_halo(halo);

}

}
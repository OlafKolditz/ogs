/**
 * \file
 * \copyright
 * Copyright (c) 2012-2020, OpenGeoSys Community (http://www.opengeosys.org)
 *            Distributed under a Modified BSD License.
 *              See accompanying file LICENSE.txt or
 *              http://www.opengeosys.org/project/license
 *
 */

#include <boost/algorithm/string.hpp>
#include <cmath>
#include <iostream>

#include "BaseLib/Algorithm.h"
#include "BaseLib/ConfigTreeUtil.h"
#include "MeshLib/Mesh.h"
#include "PhreeqcIO.h"
#include "PhreeqcIOData/AqueousSolution.h"
#include "PhreeqcIOData/Dump.h"
#include "PhreeqcIOData/EquilibriumPhase.h"
#include "PhreeqcIOData/KineticReactant.h"
#include "PhreeqcIOData/Knobs.h"
#include "PhreeqcIOData/Output.h"
#include "PhreeqcIOData/ReactionRate.h"
#include "PhreeqcIOData/Surface.h"
#include "PhreeqcIOData/UserPunch.h"

#include <iphreeqc/src/src/IPhreeqc.h>

namespace ChemistryLib
{
namespace PhreeqcIOData
{
namespace
{
template <typename DataBlock>
std::ostream& operator<<(std::ostream& os,
                         std::vector<DataBlock> const& data_blocks)
{
    std::copy(data_blocks.begin(), data_blocks.end(),
              std::ostream_iterator<DataBlock>(os));
    return os;
}
}  // namespace

PhreeqcIO::PhreeqcIO(std::string const project_file_name,
                     MeshLib::Mesh const& mesh,
                     std::string&& database,
                     std::vector<AqueousSolution>&& aqueous_solutions,
                     std::vector<EquilibriumPhase>&& equilibrium_phases,
                     std::vector<KineticReactant>&& kinetic_reactants,
                     std::vector<ReactionRate>&& reaction_rates,
                     std::vector<SurfaceSite>&& surface,
                     std::unique_ptr<UserPunch>&& user_punch,
                     std::unique_ptr<Output>&& output,
                     std::unique_ptr<Dump>&& dump,
                     Knobs&& knobs,
                     std::vector<std::pair<int, std::string>> const&
                         process_id_to_component_name_map)
    : _phreeqc_input_file(project_file_name + "_phreeqc.inp"),
      _mesh(mesh),
      _database(std::move(database)),
      _aqueous_solutions(std::move(aqueous_solutions)),
      _equilibrium_phases(std::move(equilibrium_phases)),
      _kinetic_reactants(std::move(kinetic_reactants)),
      _reaction_rates(std::move(reaction_rates)),
      _surface(std::move(surface)),
      _user_punch(std::move(user_punch)),
      _output(std::move(output)),
      _dump(std::move(dump)),
      _knobs(std::move(knobs)),
      _process_id_to_component_name_map(process_id_to_component_name_map)
{
    // initialize phreeqc instance
    if (CreateIPhreeqc() != phreeqc_instance_id)
    {
        OGS_FATAL(
            "Failed to initialize phreeqc instance, due to lack of memory.");
    }

    // load specified thermodynamic database
    if (LoadDatabase(phreeqc_instance_id, _database.c_str()) != IPQ_OK)
    {
        OGS_FATAL(
            "Failed in loading the specified thermodynamic database file: "
            "%s.",
            _database.c_str());
    }

    if (SetSelectedOutputFileOn(phreeqc_instance_id, 1) != IPQ_OK)
    {
        OGS_FATAL(
            "Failed to fly the flag for the specified file %s where phreeqc "
            "will write output.",
            _output->basic_output_setups.output_file.c_str());
    }

    if (_dump)
    {
        // Chemical composition of the aqueous solution of last time step will
        // be written into .dmp file once the second function argument is set to
        // one.
        SetDumpFileOn(phreeqc_instance_id, 1);
    }
}

void PhreeqcIO::executeInitialCalculation(
    std::vector<GlobalVector*>& process_solutions)
{
    setAqueousSolutionsOrUpdateProcessSolutions(
        process_solutions, Status::SettingAqueousSolutions);

    writeInputsToFile();

    execute();

    readOutputsFromFile();

    setAqueousSolutionsOrUpdateProcessSolutions(
        process_solutions, Status::UpdatingProcessSolutions);
}

void PhreeqcIO::doWaterChemistryCalculation(
    std::vector<GlobalVector*>& process_solutions, double const dt)
{
    setAqueousSolutionsOrUpdateProcessSolutions(
        process_solutions, Status::SettingAqueousSolutions);

    setAqueousSolutionsPrevFromDumpFile();

    writeInputsToFile(dt);

    execute();

    readOutputsFromFile();

    setAqueousSolutionsOrUpdateProcessSolutions(
        process_solutions, Status::UpdatingProcessSolutions);
}

void PhreeqcIO::setAqueousSolutionsOrUpdateProcessSolutions(
    std::vector<GlobalVector*> const& process_solutions, Status const status)
{
    std::size_t const num_chemical_systems = _mesh.getNumberOfBaseNodes();

    auto const chemical_system_map =
        *_mesh.getProperties().template getPropertyVector<std::size_t>(
            "bulk_node_ids", MeshLib::MeshItemType::Node, 1);

    // Loop over chemical systems
    for (std::size_t local_id = 0; local_id < num_chemical_systems; ++local_id)
    {
        auto const global_id = chemical_system_map[local_id];
        // Get chemical compostion of solution in a particular chemical system
        auto& aqueous_solution = _aqueous_solutions[local_id];
        auto& components = aqueous_solution.components;
        // Loop over transport process id map to retrieve component
        // concentrations from process solutions or to update process solutions
        // after chemical calculation by Phreeqc
        for (auto const& process_id_to_component_name_map_element :
             _process_id_to_component_name_map)
        {
            auto const& transport_process_id =
                process_id_to_component_name_map_element.first;
            auto const& transport_process_variable =
                process_id_to_component_name_map_element.second;

            auto& transport_process_solution =
                process_solutions[transport_process_id];

            auto component =
                std::find_if(components.begin(), components.end(),
                             [&transport_process_variable](Component const& c) {
                                 return c.name == transport_process_variable;
                             });

            if (component != components.end())
            {
                switch (status)
                {
                    case Status::SettingAqueousSolutions:
                        // Set component concentrations.
                        component->amount =
                            transport_process_solution->get(global_id);
                        break;
                    case Status::UpdatingProcessSolutions:
                        // Update solutions of component transport processes.
                        transport_process_solution->set(global_id,
                                                        component->amount);
                        break;
                }
            }

            if (transport_process_variable == "H")
            {
                switch (status)
                {
                    case Status::SettingAqueousSolutions:
                    {
                        // Set pH value by hydrogen concentration.
                        aqueous_solution.pH = -std::log10(
                            transport_process_solution->get(global_id));
                        break;
                    }
                    case Status::UpdatingProcessSolutions:
                    {
                        // Update hydrogen concentration by pH value.
                        auto hydrogen_concentration =
                            std::pow(10, -aqueous_solution.pH);
                        transport_process_solution->set(global_id,
                                                        hydrogen_concentration);
                        break;
                    }
                }
            }
        }
    }
}

void PhreeqcIO::setAqueousSolutionsPrevFromDumpFile()
{
    if (!_dump)
    {
        return;
    }

    auto const& dump_file = _dump->dump_file;
    std::ifstream in(dump_file);
    if (!in)
    {
        OGS_FATAL("Could not open phreeqc dump file '%s'.", dump_file.c_str());
    }

    std::size_t const num_chemical_systems = _mesh.getNumberOfBaseNodes();
    _dump->readDumpFile(in, num_chemical_systems);

    if (!in)
    {
        OGS_FATAL("Error when reading phreeqc dump file '%s'",
                  dump_file.c_str());
    }

    in.close();
}

void PhreeqcIO::writeInputsToFile(double const dt)
{
    DBUG("Writing phreeqc inputs into file '%s'.", _phreeqc_input_file.c_str());
    std::ofstream out(_phreeqc_input_file, std::ofstream::out);

    if (!out)
    {
        OGS_FATAL("Could not open file '%s' for writing phreeqc inputs.",
                  _phreeqc_input_file.c_str());
    }

    out << (*this << dt);

    if (!out)
    {
        OGS_FATAL("Failed in generating phreeqc input file '%s'.",
                  _phreeqc_input_file.c_str());
    }

    out.close();
}

std::ostream& operator<<(std::ostream& os, PhreeqcIO const& phreeqc_io)
{
    os << phreeqc_io._knobs << "\n";

    os << *phreeqc_io._output << "\n";

    auto const& user_punch = phreeqc_io._user_punch;
    if (user_punch)
    {
        os << *user_punch << "\n";
    }

    auto const& reaction_rates = phreeqc_io._reaction_rates;
    if (!reaction_rates.empty())
    {
        os << "RATES" << "\n";
        os << reaction_rates << "\n";
    }

    std::size_t const num_chemical_systems =
        phreeqc_io._mesh.getNumberOfBaseNodes();

    auto const chemical_system_map =
        *phreeqc_io._mesh.getProperties()
             .template getPropertyVector<std::size_t>(
                 "bulk_node_ids", MeshLib::MeshItemType::Node, 1);

    for (std::size_t local_id = 0; local_id < num_chemical_systems; ++local_id)
    {
        auto const global_id = chemical_system_map[local_id];
        auto const& aqueous_solution =
            phreeqc_io._aqueous_solutions[local_id];
        os << "SOLUTION " << global_id + 1 << "\n";
        os << aqueous_solution << "\n";

        auto const& dump = phreeqc_io._dump;
        if (dump)
        {
            auto const& aqueous_solutions_prev = dump->aqueous_solutions_prev;
            if (!aqueous_solutions_prev.empty())
            {
                os << aqueous_solutions_prev[local_id] << "\n\n";
            }
        }

        os << "USE solution none" << "\n";
        os << "END" << "\n\n";

        os << "USE solution " << global_id + 1 << "\n\n";

        auto const& equilibrium_phases = phreeqc_io._equilibrium_phases;
        if (!equilibrium_phases.empty())
        {
            os << "EQUILIBRIUM_PHASES " << global_id + 1 << "\n";
            for (auto const& equilibrium_phase : equilibrium_phases)
            {
                equilibrium_phase.print(os, global_id);
            }
            os << "\n";
        }

        auto const& kinetic_reactants = phreeqc_io._kinetic_reactants;
        if (!kinetic_reactants.empty())
        {
            os << "KINETICS " << global_id + 1 << "\n";
            for (auto const& kinetic_reactant : kinetic_reactants)
            {
                kinetic_reactant.print(os, global_id);
            }
            os << "-steps " << phreeqc_io._dt << "\n" << "\n";
        }

        auto const& surface = phreeqc_io._surface;
        if (!surface.empty())
        {
            os << "SURFACE " << global_id + 1 << "\n";
            std::size_t aqueous_solution_id =
                dump->aqueous_solutions_prev.empty()
                    ? global_id + 1
                    : num_chemical_systems + global_id + 1;
            os << "-equilibrate with solution " << aqueous_solution_id << "\n";
            os << "-sites_units DENSITY"
               << "\n";
            os << surface << "\n";
            os << "SAVE solution " << global_id + 1 << "\n";
        }

        os << "END" << "\n\n";
    }

    auto const& dump = phreeqc_io._dump;
    if (dump)
    {
        dump->print(os, num_chemical_systems);
    }

    return os;
}

void PhreeqcIO::execute()
{
    INFO("Phreeqc: Executing chemical calculation.");
    if (RunFile(phreeqc_instance_id, _phreeqc_input_file.c_str()) != IPQ_OK)
    {
        OutputErrorString(phreeqc_instance_id);
        OGS_FATAL(
            "Failed in performing speciation calculation with the generated "
            "phreeqc input file '%s'.",
            _phreeqc_input_file.c_str());
    }
}

void PhreeqcIO::readOutputsFromFile()
{
    auto const& basic_output_setups = _output->basic_output_setups;
    auto const& phreeqc_result_file = basic_output_setups.output_file;
    DBUG("Reading phreeqc results from file '%s'.",
         phreeqc_result_file.c_str());
    std::ifstream in(phreeqc_result_file);

    if (!in)
    {
        OGS_FATAL("Could not open phreeqc result file '%s'.",
                  phreeqc_result_file.c_str());
    }

    in >> *this;

    if (!in)
    {
        OGS_FATAL("Error when reading phreeqc result file '%s'",
                  phreeqc_result_file.c_str());
    }

    in.close();
}

std::istream& operator>>(std::istream& in, PhreeqcIO& phreeqc_io)
{
    // Skip the headline
    in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    std::string line;
    auto const& output = *phreeqc_io._output;
    auto const& dropped_item_ids = output.dropped_item_ids;

    auto const& surface = phreeqc_io._surface;
    int const num_skipped_lines = surface.empty() ? 1 : 2;

    std::size_t const num_chemical_systems =
        phreeqc_io._mesh.getNumberOfBaseNodes();

    auto const chemical_system_map =
        *phreeqc_io._mesh.getProperties()
             .template getPropertyVector<std::size_t>(
                 "bulk_node_ids", MeshLib::MeshItemType::Node, 1);

    for (std::size_t local_id = 0; local_id < num_chemical_systems; ++local_id)
    {
        auto const global_id = chemical_system_map[local_id];
        // Skip equilibrium calculation result of initial solution
        for (int i = 0; i < num_skipped_lines; ++i)
        {
            in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }

        // Get calculation result of the solution after the reaction
        if (!std::getline(in, line))
        {
            OGS_FATAL(
                "Error when reading calculation result of Solution %u after "
                "the reaction.",
                global_id);
        }

        std::vector<double> accepted_items;
        std::vector<std::string> items;
        boost::trim_if(line, boost::is_any_of("\t "));
        boost::algorithm::split(items, line, boost::is_any_of("\t "),
                                boost::token_compress_on);
        for (int item_id = 0; item_id < static_cast<int>(items.size());
             ++item_id)
        {
            if (std::find(dropped_item_ids.begin(), dropped_item_ids.end(),
                          item_id) == dropped_item_ids.end())
            {
                double value;
                try
                {
                    value = std::stod(items[item_id]);
                }
                catch (const std::invalid_argument& e)
                {
                    OGS_FATAL(
                        "Invalid argument. Could not convert string '%s' to "
                        "double for chemical system %d, column %d. Exception "
                        "'%s' was thrown.",
                        items[item_id].c_str(), global_id, item_id, e.what());
                }
                catch (const std::out_of_range& e)
                {
                    OGS_FATAL(
                        "Out of range error. Could not convert string '%s' to "
                        "double for chemical system %d, column %d. Exception "
                        "'%s' was thrown.",
                        items[item_id].c_str(), global_id, item_id, e.what());
                }
                accepted_items.push_back(value);
            }
        }
        assert(accepted_items.size() == output.accepted_items.size());

        auto& aqueous_solution =
            phreeqc_io._aqueous_solutions[local_id];
        auto& components = aqueous_solution.components;
        auto& equilibrium_phases = phreeqc_io._equilibrium_phases;
        auto& kinetic_reactants = phreeqc_io._kinetic_reactants;
        auto& user_punch = phreeqc_io._user_punch;
        for (int item_id = 0; item_id < static_cast<int>(accepted_items.size());
             ++item_id)
        {
            auto const& accepted_item = output.accepted_items[item_id];
            auto const& item_name = accepted_item.name;

            auto compare_by_name = [&item_name](auto const& item) {
                return item.name == item_name;
            };

            switch (accepted_item.item_type)
            {
                case ItemType::pH:
                {
                    // Update pH value
                    aqueous_solution.pH = accepted_items[item_id];
                    break;
                }
                case ItemType::pe:
                {
                    // Update pe value
                    aqueous_solution.pe = accepted_items[item_id];
                    break;
                }
                case ItemType::Component:
                {
                    // Update component concentrations
                    auto& component = BaseLib::findElementOrError(
                        components.begin(), components.end(), compare_by_name,
                        "Could not find component '" + item_name + "'.");
                    component.amount = accepted_items[item_id];
                    break;
                }
                case ItemType::EquilibriumPhase:
                {
                    // Update amounts of equilibrium phases
                    auto& equilibrium_phase = BaseLib::findElementOrError(
                        equilibrium_phases.begin(), equilibrium_phases.end(),
                        compare_by_name,
                        "Could not find equilibrium phase '" + item_name +
                            "'.");
                    (*equilibrium_phase.amount)[global_id] =
                        accepted_items[item_id];
                    break;
                }
                case ItemType::KineticReactant:
                {
                    // Update amounts of kinetic reactants
                    auto& kinetic_reactant = BaseLib::findElementOrError(
                        kinetic_reactants.begin(), kinetic_reactants.end(),
                        compare_by_name,
                        "Could not find kinetic reactant '" + item_name + "'.");
                    (*kinetic_reactant.amount)[global_id] =
                        accepted_items[item_id];
                    break;
                }
                case ItemType::SecondaryVariable:
                {
                    assert(user_punch);
                    auto& secondary_variables = user_punch->secondary_variables;
                    // Update values of secondary variables
                    auto& secondary_variable = BaseLib::findElementOrError(
                        secondary_variables.begin(), secondary_variables.end(),
                        compare_by_name,
                        "Could not find secondary variable '" + item_name +
                            "'.");
                    (*secondary_variable.value)[global_id] =
                        accepted_items[item_id];
                    break;
                }
            }
        }
    }

    return in;
}
}  // namespace PhreeqcIOData
}  // namespace ChemistryLib

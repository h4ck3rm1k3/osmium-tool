/*

Osmium -- OpenStreetMap data manipulation command line tool
http://osmcode.org/osmium

Copyright (C) 2013  Jochen Topf <jochen@topf.org>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <boost/function_output_iterator.hpp>
#include <boost/program_options.hpp>
namespace po = boost::program_options;

#include <osmium/io/any_input.hpp>
#include <osmium/io/any_output.hpp>
#include <osmium/io/output_iterator.hpp>
#include <osmium/osm/object_pointer_collection.hpp>
#include <osmium/osm/object_comparisons.hpp>

#include "command_apply_changes.hpp"

bool CommandApplyChanges::setup(const std::vector<std::string>& arguments) {
    po::variables_map vm;
    try {
        po::options_description cmdline("Allowed options");
        cmdline.add_options()
        ("output,o", po::value<std::string>(), "Output file")
        ("output-format,f", po::value<std::string>(), "Format of output file")
        ("input-format,F", po::value<std::string>(), "Format of input file")
        ("simplify,s", "Simplify change")
        ("remove-deleted,r", "Remove deleted objects from output")
        ("generator", po::value<std::string>(), "Generator setting for file header")
        ("overwrite,O", "Allow existing output file to be overwritten")
        ;

        po::options_description hidden("Hidden options");
        hidden.add_options()
        ("input-filename", po::value<std::string>(), "OSM input file")
        ("change-filenames", po::value<std::vector<std::string>>(), "OSM change input files")
        ;

        po::options_description desc("Allowed options");
        desc.add(cmdline).add(hidden);

        po::positional_options_description positional;
        positional.add("input-filename", 1);
        positional.add("change-filenames", -1);

        po::store(po::command_line_parser(arguments).options(desc).positional(positional).run(), vm);
        po::notify(vm);

        if (vm.count("input-filename")) {
            m_input_filename = vm["input-filename"].as<std::string>();
        }

        if (vm.count("change-filenames")) {
            m_change_filenames = vm["change-filenames"].as<std::vector<std::string>>();
        }

        if (vm.count("output")) {
            m_output_filename = vm["output"].as<std::string>();
        }

        if (vm.count("input-format")) {
            m_input_format = vm["input-format"].as<std::string>();
        }

        if (vm.count("output-format")) {
            m_output_format = vm["output-format"].as<std::string>();
        }

        if (vm.count("overwrite")) {
            m_output_overwrite = true;
        }

        if (vm.count("simplify")) {
            m_simplify_change = true;
        }

        if (vm.count("remove-deleted")) {
            m_remove_deleted = true;
        }

        if (vm.count("generator")) {
            m_generator = vm["generator"].as<std::string>();
        }

    } catch (boost::program_options::error& e) {
        std::cerr << "Error parsing command line: " << e.what() << std::endl;
        return false;
    }

    if ((m_output_filename == "-" || m_output_filename == "") && m_output_format.empty()) {
        std::cerr << "When writing to STDOUT you need to use the --output-format,f option to declare the file format.\n";
        return false;
    }

    if ((m_input_filename == "-" || m_input_filename == "") && m_input_format.empty()) {
        std::cerr << "When reading from STDIN you need to use the --input-format,F option to declare the file format.\n";
        return false;
    }

    m_output_file = osmium::io::File(m_output_filename, m_output_format);

    return true;
}

bool CommandApplyChanges::run() {
    std::vector<osmium::memory::Buffer> changes;
    osmium::osm::ObjectPointerCollection objects;

    for (const std::string& change_file_name : m_change_filenames) {
        osmium::io::Reader reader(change_file_name);
        while (osmium::memory::Buffer buffer = reader.read()) {
            osmium::apply(buffer, objects);
            changes.push_back(std::move(buffer));
        }
    }

    osmium::io::Reader reader(m_input_filename);

    osmium::io::Header header = reader.header();
    header.set("generator", m_generator);

    osmium::io::Writer writer(m_output_file, header, m_output_overwrite);
    osmium::io::OutputIterator<osmium::io::Writer> out(writer);

    if (m_simplify_change) {
        // If the --simplify option was given we sort with the
        // largest version of each object first and then only
        // copy this last version of any object to the output_buffer.
        objects.sort(osmium::osm::object_order_type_id_reverse_version());

        osmium::object_id_type id = 0;
        bool keep_deleted = !m_remove_deleted;

        auto output_it = boost::make_function_output_iterator([&out, &id, keep_deleted](const osmium::Object& obj) {
            if (obj.id() != id) {
                if (keep_deleted || obj.visible()) {
                    *out = obj;
                }
                id = obj.id();
            }
        });

        std::set_union(objects.begin(),
                       objects.end(),
                       osmium::io::InputIterator<osmium::io::Reader, osmium::Object> {reader},
                       osmium::io::InputIterator<osmium::io::Reader, osmium::Object> {},
                       output_it,
                       osmium::osm::object_order_type_id_reverse_version());
    } else {
        // If the --simplify option was not given, this
        // is a straightforward sort of the change files
        // and the a merge with the input file.
        objects.sort(osmium::osm::object_order_type_id_version());

        std::set_union(objects.begin(),
                       objects.end(),
                       osmium::io::InputIterator<osmium::io::Reader, osmium::Object> {reader},
                       osmium::io::InputIterator<osmium::io::Reader, osmium::Object> {},
                       out);
    }

    out.flush();
    writer.close();

    return true;
}

namespace {

    const bool register_apply_changes_command = CommandFactory::add("apply-changes", "Apply OSM change files to OSM data file", []() {
        return new CommandApplyChanges();
    });

}


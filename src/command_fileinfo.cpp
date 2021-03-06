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

#include <iostream>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <boost/program_options.hpp>
namespace po = boost::program_options;

#include <crypto++/sha.h>

#include <osmium/io/any_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/osm/entity_flags.hpp>
#include <osmium/osm/ostream.hpp>
#include <osmium/visitor.hpp>

#include "command_fileinfo.hpp"

bool CommandFileinfo::setup(const std::vector<std::string>& arguments) {
    po::variables_map vm;
    try {
        po::options_description cmdline("Allowed options");
        cmdline.add_options()
        ("extended,e", "Extended output")
        ("input-format,F", po::value<std::string>(), "Format of input file")
        ;

        po::options_description hidden("Hidden options");
        hidden.add_options()
        ("input-filename", po::value<std::string>(), "Input file")
        ;

        po::options_description desc("Allowed options");
        desc.add(cmdline).add(hidden);

        po::positional_options_description positional;
        positional.add("input-filename", 1);

        po::store(po::command_line_parser(arguments).options(desc).positional(positional).run(), vm);
        po::notify(vm);

        if (vm.count("extended")) {
            m_extended = true;
        }

        if (vm.count("input-format")) {
            m_input_format = vm["input-format"].as<std::string>();
        }

        if (vm.count("input-filename")) {
            m_input_filename = vm["input-filename"].as<std::string>();
        }

    } catch (boost::program_options::error& e) {
        std::cerr << "Error parsing command line: " << e.what() << std::endl;
        return false;
    }

    if ((m_input_filename == "-" || m_input_filename == "") && m_input_format.empty()) {
        std::cerr << "When reading from STDIN you need to use the --input-format,F option to declare the file format.\n";
        return false;
    }

    m_input_file = osmium::io::File(m_input_filename, m_input_format);

    return true;
}

struct InfoHandler : public osmium::handler::Handler {

    osmium::Box bounds;
    uint64_t nodes = 0;
    uint64_t ways = 0;
    uint64_t relations = 0;
    CryptoPP::SHA hash {};

    void node(const osmium::Node& node) {
        hash.Update(node.data(), node.byte_size());
        bounds.extend(node.location());
        ++nodes;
    }

    void way(const osmium::Way& way) {
        hash.Update(way.data(), way.byte_size());
        ++ways;
    }

    void relation(const osmium::Relation& relation) {
        hash.Update(relation.data(), relation.byte_size());
        ++relations;
    }

}; // InfoHandler

off_t filesize(const std::string& filename) {
    if (filename.empty()) {
        return 0;
    }
    struct stat s;
    stat(filename.c_str(), &s);
    return s.st_size;
}

bool CommandFileinfo::run() {
    try {
        std::cout << "File:\n";
        std::cout << "  Name: " << m_input_filename << "\n";
        std::cout << "  Format: " << m_input_file.format() << "\n";
        std::cout << "  Compression: " << m_input_file.compression() << "\n";

        if (!m_input_file.filename().empty()) {
            std::cout << "  Size: " << filesize(m_input_file.filename()) << "\n";
        }

        osmium::io::Reader reader(m_input_file, m_extended ? osmium::osm_entity::flags::all : osmium::osm_entity::flags::nothing);

        osmium::io::Header header = reader.header();
        std::cout << "Header:\n";

        std::cout << "  Bounding boxes:\n";
        for (auto& box : header.boxes()) {
            std::cout << "    " << box << "\n";
        }
        std::cout << "  With history: " << (header.has_multiple_object_versions() ? "yes" : "no") << "\n";

        std::cout << "  Options:\n";
        for (auto option : header) {
            std::cout << "    " << option.first << "=" << option.second << "\n";
        }

        if (m_extended) {
            InfoHandler info_handler;
            osmium::apply(reader, info_handler);
            std::cout << "Data: " << "\n";
            std::cout << "  Bounding box: " << info_handler.bounds << "\n";

            unsigned char digest[CryptoPP::SHA::DIGESTSIZE];
            info_handler.hash.Final(digest);
            std::cout << "  SHA: " << std::hex;
            for (int i=0; i < CryptoPP::SHA::DIGESTSIZE; ++i) {
                std::cout << static_cast<int>(digest[i]);
            }
            std::cout << std::dec << "\n";
            std::cout << "  Number of nodes: " << info_handler.nodes << "\n";
            std::cout << "  Number of ways: " << info_handler.ways << "\n";
            std::cout << "  Number of relations: " << info_handler.relations << "\n";
        }

        return true;
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
        return false;
    }
}

namespace {

    const bool register_fileinfo_command = CommandFactory::add("fileinfo", "Show information about OSM file", []() {
        return new CommandFileinfo();
    });

}


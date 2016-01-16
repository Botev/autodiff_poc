//
// Created by alex on 18/12/15.
//

#ifndef AUTODIFF_BACKENDS_ARRAYFIRE_H
#define AUTODIFF_BACKENDS_ARRAYFIRE_H

#include <fstream>
namespace metadiff{

    class ArrayfireBackend: public FunctionBackend<af::array>{
    public:
        std::string include_path;
        std::string lib_path;
        ArrayfireBackend(std::string include_path,
                         std::string lib_path):
                include_path(include_path),
                lib_path(lib_path)
        {};

        ArrayfireBackend(){
            // Create backend and compile function
            const char *AF_PATH = getenv("AF_PATH") ? getenv("AF_PATH") : "/opt/arrayfire-3";
            include_path = std::string(AF_PATH) + "/include";
            lib_path = std::string(AF_PATH) + "/lib";
        };

        ArrayfireBackend(std::string AF_PATH){
            // Create backend and compile function
            include_path = AF_PATH + "/include";
            lib_path = AF_PATH + "/lib";
        };

        void compile_file(std::string file_name, std::string dll_name){
            std::string command = "MKL_NUM_THREADS=4 g++ -O3 -Wall -shared -fPIC -std=c++11 -laf ";
            command += "-Werror=return-type -Wno-unused-variable -Wno-narrowing ";
            command += " -I" + include_path;
            command += " -L" + lib_path;
//            command += " -I./";
            command += " -o " + dll_name + " " + file_name;
            std::cout << "Command: " << command << std::endl;
            std::cout << "Compilation response: " << system(command.c_str()) << std::endl;
            return;
        }

        void generate_source(std::string file_name, Graph graph,
                             std::vector<Node> inputs,
                             std::vector<Node> targets,
                             Updates& updates) {
            std::ofstream f;
            f.open(file_name);
            std::string tabs = "";
            // Disclaimer
            f << "// Auto generated by Metadiff\n// Please do not edit\n\n";
            // Includes
            f <<    "#include \"vector\"\n"
                    "#include \"memory\"\n"
                    "#include <exception>\n"
                    "#include <arrayfire.h>\n";
            f << "\n";
            // Write the interface definitions
            this->write_interface(f);

            f << "void print_mem_info(std::string name){\n"
                    "    size_t alloc_bytes,alloc_buffers,lock_bytes,lock_buffers;\n"
                    "    af::deviceMemInfo(&alloc_bytes,&alloc_buffers,&lock_bytes,&lock_buffers);\n"
                    "    std::cout << \"Memory info\" << name << std::endl;\n"
                    "    std::cout << \"Allocated: \" << alloc_bytes / 1024 << \" KB\" << std::endl;\n"
                    "    std::cout << \"Buffers allocated: \" << alloc_buffers << std::endl;\n"
                    "    std::cout << \"In use: \" << lock_bytes / 1024 << \" KB\" << std::endl;\n"
                    "    std::cout << \"Buffers in use: \" << lock_buffers << std::endl;\n"
                    "    return;\n"
                    "};\n\n";

            f << "\n";
            f << "extern \"C\" std::vector<af::array> eval_func(std::vector<af::array>& inputs, std::vector<SharedPtr>& shared_vars){\n";
//            f << "\tstd::cout << \"ADS\" << std::endl;\n";
            f << "\tstd::vector<af::array> outputs;\n";
//            f << "print_mem_info(\"Initial\");\n";
//            f << "\taf::setBackend(AF_BACKEND_OPENCL);\n";
            f << "\t// Set up automatic broadcasting\n";
            f << "\taf::gforSet(true);\n";
            // Get ancestors mask
            graph->add_temporary_updates(updates);
            auto ancestor_mask = graph->get_ancestors_mask(targets);
            // Check that all inputs required are given
            for(int i=0;i<ancestor_mask.size();i++){
                if(ancestor_mask[i] and graph->nodes[i]->type == INPUT){
                    for(int j=0;j<=inputs.size();j++){
                        if(j == inputs.size()){
                            throw MissingRequiredInput(targets, i);
                        }
                        if(inputs[j].id == i){
                            break;
                        }
                    }
                }
            }

            // Write all of the input nodes
            f << "\n\t// Set all of the inputs accordingly\n";
            for(int i=0;i<inputs.size();i++){
                f << "\taf::array node_" << inputs[i].id << " = inputs[" << i << "];\n";
            }

//            f << "print_mem_info(\"Post input\");\n";

            // Write all of the shared variables
            f << "\n\t// Set all of the shared variables accordingly\n";
            for(int i=0;i<graph->shared_vars.size();i++){
//                f << "\tstd::cout << \"Node \" << " << graph->shared_vars[i]->id << " << std::endl;\n";
                f << "\taf::array node_" << graph->shared_vars[i]->id << " = shared_vars[" << i << "]->value;\n";
//                f << "\tstd::cout << node_" << graph->shared_vars[i]->id << ".dims() << std::endl;\n";
            }

//            f << "print_mem_info(\"Post shared\");\n";

            // Calculate all of the symbolic integers
            calculate_symbolics(f, graph, inputs);
            // Validate input shapes
            validate_input_shapes(f, graph, inputs);

            // Calculate all of the other nodes
            f << "\n\t// Calculate all of the computation nodes\n";
            for(int i=0;i<ancestor_mask.size();i++){
                if(ancestor_mask[i] and graph->nodes[i]->type != INPUT){
                    calculate_node(f, graph, i);
//                    f << "print_mem_info(\"Post " << i << "\");\n";
                }
            }

//            f << "std::cout << \"End\" << std::endl;\n";

            // Disable the automatic broadcasting
            f << "\taf::gforSet(false);";

            // Update all of the shared_variables
            f << "\n\t// Update all shared variables\n";
            for(int i=0;i<graph->nodes.size();i++){
                auto node = graph->nodes[i];
                if(node->type == UPDATE){
                    auto shared_id = node->op->get_arguments()[0].lock()->id;
                    auto update_id = node->op->get_parents()[0].lock()->id;
//                    std::cout << "Update id: " << i << std::endl;
//                    std::cout << "shared id: " <<  shared_id << std::endl;
//                    std::cout << "expression id: " << update_id << std::endl;
//                    std::cout << "shared num " << graph->shared_vars.size() << std::endl;
                    for(int j=0;j<graph->shared_vars.size();j++){
//                        std::cout << "this shared: " <<  graph->shared_vars[j]->id << std::endl;
                        if(graph->shared_vars[j]->id == shared_id){
                            f << "\tshared_vars[" << j << "]->value = "
                                    "node_" << update_id << ";\n";
                        }
                    }
                }
            }
            graph->clear_temprary_updates();

//            //   Debugging
//            f << "\taf_print(node_18);\n";
//            f << "\taf_print(node_19_cond);\n";
//            f << "\taf_print(node_19);\n";
//            f << "\taf_print(node_21);\n";
//            f << "\taf_print(node_22);\n";
//            f << "\taf_print(node_23);\n";
//            f << "\taf_print(node_24);\n";
//            f << "\taf_print(node_25);\n";

//            f << "\taf::sync();\n";
            // Write all of the output nodes as the result
            f << "\n\t// Write all of the output nodes in correct order\n";
            f << "\treturn {";
            for(int i=0;i<targets.size();i++){
                if(i < targets.size() - 1){
                    f << "node_" << targets[i].id << ", ";
                } else {
                    f << "node_" << targets[i].id << "};\n";
                }
            }
            f << "}\n";
            f.close();
        }

        void calculate_symbolics(std::ofstream& f, Graph graph, std::vector<Node> inputs){
            f << "\n\t// Set all of the symbolic variables\n";
            for(size_t i=0;i<graph->sym_integer_count;i++){
                SymInt variable = SymInt::variable(i);
                f << "\tint " << variable << " = ";
                bool done = false;
                for(int j=0;j<inputs.size();j++){
                    auto shape = graph->nodes[inputs[j].id]->shape;
                    for(int s=0;s<4;s++){
                        if(shape[s] == variable){
                            f << "node_" << inputs[j].id << ".dims(" << s << ")";
                            done = true;
                            break;
                        }
                    }
                    if(done){
                        break;
                    }
                }
                f << ";\n";
            }
        }

        void validate_input_shapes(std::ofstream& f, Graph graph, std::vector<Node> inputs){
            f <<"\n\t// Verify input sizes are correct\n";
            for(int i=0;i<inputs.size();i++){
                auto node = graph->nodes[i];
                f << "\tsize_t node_" << node->id << "_expected_shape[4]{";
                for(int j=0;j<4;j++){
                    f << node->shape[j].to_string_with_star();
                    if(j<3){
                        f << ", ";
                    }
                }
                f << "};\n";
                f << "\tsize_t node_" << node->id << "_actual_shape[4]{";
                for(int j=0;j<4;j++){
                    f << "node_" << node->id << ".dims(" << j << ")";
                    if(j<3){
                        f << ", ";
                    }
                }
                f << "};\n";
                f << "\tif(node_" << node->id << "_expected_shape[0] != node_" << node->id << "_actual_shape[0]\n"
                        "\t\tor node_" << node->id << "_expected_shape[1] != node_" << node->id << "_actual_shape[1]\n"
                        "\t\tor node_" << node->id << "_expected_shape[2] != node_" << node->id << "_actual_shape[2]\n"
                        "\t\tor node_" << node->id << "_expected_shape[3] != node_" << node->id << "_actual_shape[3]){\n"
                        "\t\t throw InvalidInputShape(" << node->id <<
                ", node_" << node->id << "_expected_shape, node_" << node->id << "_actual_shape);\n"
                        "\t}\n";
            }
        }

        static void calculate_node(std::ofstream& f, Graph graph, size_t id){
            auto node = graph->nodes[id];
            auto op_name = node->op->name;
            auto parents = node->op->get_parents();
            auto args = node->op->get_arguments();
            auto children = node->children;
//        f << "\tstd::cout << \"Parents dims: \" << ";
//        for(int i=0;i<parents.size();i++){
//            if(not parents[i].lock()->is_constant()) {
//                f << "node_" << parents[i].lock()->id << ".dims() << \"|\" << ";
//            }
//        }
//        f << "std::endl;\n";

            if(node->type == CONSTANT and node->op->name == "Input"){
                f << "\taf::array node_" << id << " = af::constant(";
                if(node->v_type == FLOAT) {
                    float host[1];
                    node->value.host(host);
                    f << host[0];
                } else if(node->v_type == INTEGER){
                    int host[1];
                    node->value.host(host);
                    f << host[0];
                } else {
                    bool host[1];
                    node->value.host(host);
                    f << host[0];
                }
                for(int i=0;i<4;i++){
                    f << ", " << node->shape[i].to_string_with_star();
                }
                f << ");\n";
            } else if(node->type != UPDATE and node->type != SHARED_INPUT) {
//                f << "\tstd::cout << \"Node \" << " << id << " << std::endl;\n";
                if (op_name == "Broadcast") {
                    f << "\taf::array node_" << id << " = ";
                    bool not_supproted = false;
                    for (int i = 0; i < children.size(); i++) {
                        auto name = children[i].lock()->op->name;
                        if (name != "Add" and name != "Mul"
                            and name != "Neg" and name != "Div") {
                            not_supproted = true;
                            break;
                        }
                    }
                    if (not_supproted) {
                        auto parent = parents[0].lock();
                        f << "af::tile(node_" << parent->id << ", ";
                        for (int i = 0; i < 4; i++) {
                            if (node->shape[i] != parent->shape[i]) {
                                f << node->shape[i].to_string_with_star();
                            } else {
                                f << "1";
                            }
                            if (i < 3) {
                                f << ", ";
                            }
                        }
                        f << ")";
                    } else {
                        auto parent = parents[0].lock();
                        f << "node_" << parent->id;
                    }
                } else if (op_name == "Sum") {
                    f << "\taf::array node_" << id << " = ";
                    auto parent = parents[0].lock();
                    auto axes = dynamic_cast<Sum *>(node->op.get())->axes;
                    std::string code = "node_" + std::to_string(parent->id);
                    if(node->is_scalar()){
                        code = "af::sum(af::flat(node_" + std::to_string(parent->id) + "))";
                    } else {
                        for (int i = 0; i < axes.size(); i++) {
                            if (parent->shape[axes[i]] != 1) {
                                code = "af::sum(" + code + ", " + std::to_string(axes[i]) + ")";
                            }
                        }
                    }
                    f << code;
                } else if (op_name == "Add") {
                    f << "\taf::array node_" << id << " = ";
                    for (int i = 0; i < parents.size(); i++) {
                        f << "node_" << parents[i].lock()->id;
                        if (i < parents.size() - 1) {
                            f << " + ";
                        }
                    }
                } else if (op_name == "Neg") {
                    f << "\taf::array node_" << id << " = ";
                    f << "- node_" << parents[0].lock()->id << "";
                } else if (op_name == "Mul") {
                    f << "\taf::array node_" << id << " = ";
                    for (int i = 0; i < parents.size(); i++) {
                        f << "node_" << parents[i].lock()->id;
                        if (i < parents.size() - 1) {
                            f << " * ";
                        }
                    }
                } else if (op_name == "Div") {
                    f << "\taf::array node_" << id << " = ";
                    f << "1 / node_" << parents[0].lock()->id << "";
                } else if (op_name == "Square") {
                    f << "\taf::array node_" << id << " = ";
                    f << "node_" << parents[0].lock()->id << " * "
                    << parents[0].lock()->id << "";
                } else if (op_name == "Transpose") {
                    f << "\taf::array node_" << id << " = ";
                    f << "af::transpose(node_" << parents[0].lock()->id << ")";
                } else if (op_name == "MatrixMul") {
                    f << "\taf::array node_" << id << " = ";
                    f << "af::matmul(";
                    for (int i = 0; i < parents.size(); i++) {
                        f << "node_" << parents[i].lock()->id;
                        if (i < parents.size() - 1) {
                            f << ", ";
                        }
                    }
                    f << ")";

//                    for (int i = 0; i < parents.size(); i++) {
//                        f << "\tauto node_" << parents[i].lock()->id << "_" << id << "_ptr = "
//                                "node_" << parents[i].lock()->id << ".host<float>();\n";
//                        f << "\tauto node_" << parents[i].lock()->id << "_" << id << "_dims = "
//                                "node_" << parents[i].lock()->id << ".dims();\n";
//                    }
//                    f << "\taf::setBackend(AF_BACKEND_CPU);\n";
//                    for (int i = 0; i < parents.size(); i++) {
//                        f << "\taf::array node_" << parents[i].lock()->id << "_" << id << "_cpu("
//                                "node_" << parents[i].lock()->id << "_" << id << "_dims, "
//                                "node_" << parents[i].lock()->id << "_" << id << "_ptr, afDevice);\n";
//                    }
//                    f << "\taf::array node_" << id << "_cpu = ";
//                    f << "af::matmul(";
//                    for (int i = 0; i < parents.size(); i++) {
//                        f << "node_" << parents[i].lock()->id << "_" << id << "_cpu";
//                        if (i < parents.size() - 1) {
//                            f << ", ";
//                        }
//                    }
//                    f << ");\n";
//                    for (int i = 0; i < parents.size(); i++) {
//                        f << "\tdelete[] node_" << parents[i].lock()->id << "_" << id << "_ptr;\n";
//                    }
//                    f << "\tauto node_" << id << "_ptr = "  << "node_" << id << "_cpu.host<float>();\n";
//                    f << "\tauto node_" << id << "_dims = "  << "node_" << id << "_cpu.dims();\n";
//                    f << "\taf::setBackend(AF_BACKEND_OPENCL);\n";
//                    f << "\taf::array node_" << id << "("  << "node_" << id << "_dims, "
//                            "node_" << id << "_ptr, afHost);\n";
//                    f << "\tdelete[] node_" << id << "_ptr";


//                    f << "std::cout << node_" << id << ".dims() << std::endl\n;\n";
//                    f << "\tfor(int i=0;i<10;i++){\n"
//                    "\t\tnode_" << id << " = " << "af::matmul(node_" <<  parents[0].lock()->id <<
//                            ", node_" << id << ");\n"
//                    "\t}";
                } else if (op_name == "MatrixInv") {
                    f << "\taf::array node_" << id << " = ";
                    f << "af::inverse(node_" << parents[0].lock()->id << ")";
                } else if (op_name == "Det") {
                    f << "\taf::array node_" << id << " = ";
                    f << "af::det(node_" << parents[0].lock()->id << ")";
                } else if (op_name == "LogDet") {
                    f << "\taf::array node_" << id << " = ";
                    f << "af::log(af::det(node_" << parents[0].lock()->id << "))";
                } else if (op_name == "Trace") {
                    f << "\taf::array node_" << id << " = ";

                } else if (op_name == "Exp") {
                    f << "\taf::array node_" << id << " = ";
                    f << "af::exp(node_" << parents[0].lock()->id << ")";
                } else if (op_name == "Log") {
                    f << "\taf::array node_" << id << " = ";
                    f << "af::log(node_" << parents[0].lock()->id << ")";
                } else if (op_name == "Pow") {
                    f << "\taf::array node_" << id << " = ";

                } else if (op_name == "Abs") {
                    f << "\taf::array node_" << id << " = ";
                    f << "af::abs(node_" << parents[0].lock()->id << ")";
                } else if (op_name == "Sin") {
                    f << "\taf::array node_" << id << " = ";
                    f << "af::sin(node_" << parents[0].lock()->id << ")";
                } else if (op_name == "Cos") {
                    f << "\taf::array node_" << id << " = ";
                    f << "af::cos(node_" << parents[0].lock()->id << ")";
                } else if (op_name == "Tan") {
                    f << "\taf::array node_" << id << " = ";
                    f << "af::tan(node_" << parents[0].lock()->id << ")";
                } else if (op_name == "Cot") {
                    f << "\taf::array node_" << id << " = ";
                    f << "af::cot(node_" << parents[0].lock()->id << ")";
                } else if (op_name == "Sinh") {
                    f << "\taf::array node_" << id << " = ";
                    f << "af::sinh(node_" << parents[0].lock()->id << ")";
                } else if (op_name == "Cosh") {
                    f << "\taf::array node_" << id << " = ";
                    f << "af::cosh(node_" << parents[0].lock()->id << ")";
                } else if (op_name == "Tanh") {
                    f << "\taf::array node_" << id << " = ";
                    f << "af::tanh(node_" << parents[0].lock()->id << ")";
                } else if (op_name == "Coth") {
                    f << "\taf::array node_" << id << " = ";
                    f << "af::coth(node_" << parents[0].lock()->id << ")";
                } else if (op_name == "Sigmoid") {
                    f << "\taf::array node_" << id << " =  1 / (1 + af::exp(-node_" << parents[0].lock()->id << "))";
                } else if (op_name == "Diag") {
                    f << "\taf::array node_" << id << " = ";
                    if (node->shape[1] == 1) {
                        f << "af::diag(node_" << parents[0].lock()->id << ", 0, true)";
                    } else {
                        f << "af::diag(node_" << parents[0].lock()->id << ", 0, false)";
                    }
                } else if (op_name == "Reshape") {
                    f << "\taf::array node_" << id << " = ";
                    f << "af::moddims(node_" << parents[0].lock()->id << ", ";
                    for (int i = 0; i < 4; i++) {
                        f << node->shape[i].to_string_with_star();
                        if (i < 3) {
                            f << ", ";
                        }
                    }
                    f << ")";
                } else if (op_name == "Reorder") {
                    f << "\taf::array node_" << id << " = ";
                    f << "af::reorder(node_" << parents[0].lock()->id << ", ";
                    auto op_1 = dynamic_cast<Reorder *>(node->op.get());
                    auto order = op_1->order;
                    for (int i = 0; i < 4; i++) {
                        f << order[i];
                        if (i < 3) {
                            f << ", ";
                        }
                    }
                    f << ")";
                } else if (op_name == "Softplus") {
                    auto parent = parents[0].lock();
                    double th = dynamic_cast<Softplus *>(node->op.get())->threshold;
                    f << "\taf::array node_" << id << "_cond = node_" << parent->id << " < " << th << ";\n";
                    f << "\taf::array node_" << id << "_exp = af::exp(node_" << parent->id << ");\n";
                    f << "\taf::array node_" << id << " = af::log1p(node_" << id << "_exp);\n";
                    f << "\taf::replace(node_" << id << ", node_" << id << "_cond, node_" << parent->id << ")";
                } else if (op_name == "BinCrossEntropyLogit") {
                    auto p = parents[0].lock();
                    auto sf = parents[1].lock();
                    auto sfm = parents[2].lock();
                    // Calculate p*(sf(-x)-sf(x)) + sf(x)
                    f << "\taf::array node_" << id << " = " << "node_" << p->id << " * (node_" << sfm->id
                    << " - node_" << sf->id << ") + node_" << sf->id;
                } else if (op_name == "Select") {
                    auto trueParent = parents[0].lock();
                    auto falseParent = parents[1].lock();
                    auto condition = node->op->get_arguments()[0].lock();
                    f << "\taf::array node_" << id << " = " << "select(node_" << condition->id <<
                    ", node_" << trueParent->id << ", node_" << falseParent->id << ")";
                } else if (op_name == "MaxAndArgMax") {

                } else if (op_name == "SortAndArgSort") {

                } else if (op_name == "Eye") {

                } else if (op_name == "Gt") {
                    f << "\taf::array node_" << id << " = ";
                    f << "node_" << parents[0].lock()->id << " > "
                            "node_" << parents[1].lock()->id;
                } else if (op_name == "Ge") {
                    f << "\taf::array node_" << id << " = ";
                    f << "node_" << parents[0].lock()->id << " >= "
                            "node_" << parents[1].lock()->id;
                } else if (op_name == "Lt") {

                } else if (op_name == "Le") {

                } else if (op_name == "Eq") {

                } else if (op_name == "Ne") {

                } else if (op_name == "ApproxEq") {

                } else if (op_name == "ApproxNe") {

                } else if (op_name == "ZeroElem") {

                } else if (op_name == "NonZeroElem") {

                } else if (op_name == "IsNaN") {

                } else if (op_name == "IsInf") {

                } else {
                    f << "WTF" << node->id << " " << node->type;
                }
                f << ";\n";
                if(op_name == "MatrixMul"){
//                    f << "\taf::setBackend(AF_BACKEND_OPENCL);\n";
                }
//                f << "\tstd::cout << " << id << " << node_" << id << ".dims() << std::endl;\n";
            }
//        f << "\taf_print(af::anyTrue(af::anyTrue(af::isNaN(node_" << id <<"))));\n";
        }

        void print_shape_esception(std::ofstream& f){
            f << "class InvalidInputShape: public std::exception{\n"
                    "    public:\n"
                    "        size_t id;\n"
                    "        af::dim4 expected;\n"
                    "        af::dim4 given;\n"
                    "        std::string msg;\n"
                    "        InvalidInputShape(size_t id,\n"
                    "                          af::dim4 expected,\n"
                    "                          af::dim4 given):\n"
                    "                id(id),\n"
                    "                expected(expected),\n"
                    "                given(given)\n"
                    "        {\n"
                    "            msg = \"The input node with id \" + std::to_string(id) + \" provided has incorrect shape.\\n\" +\n"
                    "                  \"Expected:\" + std::to_string(expected[0]) + \", \" + std::to_string(expected[1]) + \", \"\n"
                    "                  + std::to_string(expected[2]) + \", \" + std::to_string(expected[3]) + \", \" +\"\\n\" +\n"
                    "                  \"Given:   \" + std::to_string(given[0]) + \", \" + std::to_string(given[1]) + \", \"\n"
                    "                  + std::to_string(given[2]) + \", \" + std::to_string(given[3]) + \", \" +\"\\n\";\n"
                    "        };\n"
                    "\n"
                    "        const char* what() const throw(){\n"
                    "            return msg.c_str();\n"
                    "        }\n"
                    "    };\n\n";
        }
    };
}

#endif //AUTODIFF_BACKENDS_ARRAYFIRE_H

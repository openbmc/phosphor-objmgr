/**
 * Copyright © 2017 IBM Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * This application will check the ActiveState property
 * on the source unit passed in.  If that state is 'failed',
 * then it will either stop or start the target unit, depending
 * on the command line arguments.
 */
#include "monitor.hpp"

#include <CLI/CLI.hpp>

#include <map>
#include <string>

using namespace phosphor::unit::failure;

static const std::map<std::string, Monitor::Action> actions = {
    {"start", Monitor::Action::start}, {"stop", Monitor::Action::stop}};

int main(int argc, char** argv)
{
    CLI::App app;
    std::string source;
    std::string target;
    Monitor::Action action{Monitor::Action::start};

    app.add_option("-s,--source", source, "The source unit to monitor")
        ->required();
    app.add_option("-t,--target", target, "The target unit to start or stop")
        ->required();
    app.add_option("-a,--action", action, "Target unit action - start or stop")
        ->required()
        ->transform(CLI::CheckedTransformer(actions, CLI::ignore_space));

    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError& e)
    {
        return (app).exit(e);
    }
    Monitor monitor{source, target, action};

    monitor.analyze();

    return 0;
}

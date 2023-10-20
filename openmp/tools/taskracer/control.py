#!/usr/bin/python3
import networkx as nx
from networkx.readwrite import json_graph

import json
from datetime import datetime
import os
import subprocess
import argparse
import logging

# Configure the logging
logging.basicConfig(level=logging.DEBUG, format="%(asctime)s - %(levelname)s - (%(filename)s:%(lineno)d) %(message)s ")
logger = logging.getLogger()

format_data = "%y%m%d-%H%M%S"
x = datetime.now()
time = x.strftime(format_data)

dataDir = os.path.join(os.getcwd(),"data")
inputfile = ""
runlogfile = os.path.join(dataDir, "runlog.txt")


def check_path_exists(args) -> bool:
    global dataDir
    global inputfile

    if not os.path.exists(dataDir):
        os.mkdir(dataDir)

    if not os.path.exists(runlogfile):
        os.mknod(runlogfile)

    inputfile = os.path.join(dataDir, args.input)
    if not os.path.exists(inputfile) and args.exe is None:
        logger.error(f"{inputfile} does not exist!")
        return False

    if args.exe is not None:
        executable = os.path.join(os.getcwd(), args.exe)
        if not os.path.exists(executable):
            logger.error(f"{executable} does not exist!")
            return False

    return True


def write_to_file(path: str, content:str):
    with open(path, 'w') as f:
        f.write(content)
    return


def run_cmd(cmd: str, dir: str):
    # Use subprocess.Popen to run the command and capture output
    process = subprocess.Popen(
        cmd,
        cwd=dir,
        shell=True,  # Allows running commands with shell features like pipes (use with caution)
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    # Wait for the command to complete and capture stdout and stderr
    stdout, stderr = process.communicate()

    # Check the return code to see if the command was successful
    return_code = process.returncode
    
    return stdout.decode('utf-8'), stderr.decode('utf-8'), return_code


def main(args):
    global inputfile
    global runlogfile
    
    if args.exe is not None:
        logger.info(f"we have a program to run first {args.exe}")

        dir = os.getcwd()
        cmd = f"./{args.exe}"
        stdout, stderr, flag = run_cmd(cmd, dir)
        if flag != 0:
            logger.error(f"error while running {args.exe}")
            logger.error(stderr)
            return
        write_to_file(runlogfile, stdout)

    if not check_path_exists(args):
        return
    
    G = nx.read_graphml(inputfile)

    # Find and remove nodes with attribute "vertex_id" equal to 0
    nodes_to_remove = [node for node, data in G.nodes(data=True) if data.get('vertex_id') == 0]
    G.remove_nodes_from(nodes_to_remove)

    logger.info(f"G is acyclic: {nx.is_directed_acyclic_graph(G)}")

    data = json_graph.node_link_data(G)

    for node in data['nodes']:
        node["active"] = True
        node["hidden"] = False

    for edge in data['links']:
        edge["hidden"] = False

    jsonData = json.dumps(data, indent=4)

    if not args.test:
        outputname = f'output{time}.json'
        if args.exe is not None:
            binaryname = (args).exe.split("/")[-1]
            outputname = f'{binaryname}-{time}.json'
        outputpath = os.path.join(dataDir, outputname)
        write_to_file(outputpath, jsonData)
        logger.info(f'Conversion completed. JSON file saved as {outputpath}')
    else:
        logger.info(f'Conversion completed. Only a test, no JSON file created')


if __name__ == "__main__":

    parser = argparse.ArgumentParser(description="OpenMP run&vis program")
    parser.add_argument("--input", help="input file name", type=str, default="rawgraphml.txt")
    parser.add_argument("--exe", help="the OpenMP program to run", type=str)

    parser.add_argument('--test', action='store_true', help="mark this run as a test, no output json file created.")
    parser.set_defaults(test=False)

    args = parser.parse_args()

    main(args)
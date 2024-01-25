#!/usr/bin/python3
import networkx as nx
from networkx.readwrite import json_graph

import json
from datetime import datetime
import os
import subprocess
import argparse
import logging
import csv
from enum import IntEnum


class returnCode(IntEnum):
    SUCCESS = 0
    ERROR = 1


# Configure the logging
logging.basicConfig(level=logging.DEBUG, format="%(asctime)s - %(levelname)s - (%(filename)s:%(lineno)d) %(message)s ")
logger = logging.getLogger()

format_data = "%y%m%d-%H%M%S"
x = datetime.now()
time = x.strftime(format_data)

dataDir = ""
rawgraphFile = ""
runlogfile = ""
movementlogfile = ""


def check_path_exists(args) -> returnCode:
    global dataDir
    global rawgraphFile
    global runlogfile
    global movementlogfile

    dataDir = os.path.join(os.getcwd(),"data")
    if args.dir is not None:
        exe_dir = args.dir
        if not os.path.exists(exe_dir):
            logger.error(f"{exe_dir} does not exist!")
            return returnCode.ERROR
        dataDir = os.path.join(exe_dir, "data")
    elif args.exe is not None:
        exe = os.path.join(os.getcwd(), args.exe)
        if not os.path.exists(exe):
            logger.error(f"{exe} does not exist!")
            return returnCode.ERROR
    
    rawgraphFile = os.path.join(dataDir, args.rawgraphFile)
    runlogfile = os.path.join(dataDir, "runlog.txt")
    movementlogfile = os.path.join(dataDir, "movement.txt")

    if not os.path.exists(dataDir):
        os.mkdir(dataDir)

    if not os.path.exists(runlogfile):
        os.mknod(runlogfile)

    if not os.path.exists(movementlogfile):
        os.mknod(movementlogfile)

    if not os.path.exists(rawgraphFile):
        os.mknod(rawgraphFile)

    return returnCode.SUCCESS


def write_to_file(path: str, content:str):
    with open(path, 'w') as f:
        f.write(content)
    return


def run_cmd(cmd: str, dir: str, log=False) -> [str,str,int]:
    if log is True:
        logger.info(f"cmd: {cmd}, dir: {dir}")

    # Use subprocess.Popen to run the command and capture output
    process = subprocess.Popen(
        cmd,
        cwd=dir,
        shell=True,  # Allows running commands with shell features like pipes (use with caution)
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    try:
        stdout, stderr = process.communicate(timeout=60)
        return_code = process.returncode
        return stdout.decode('utf-8'), stderr.decode('utf-8'), return_code
    except Exception as e:
        logger.error(e)
        return "", str(e), -1


def run_exe(exe:str, logfile:str, dir=None) -> returnCode:
    logger.info(f"running program {exe}")

    if dir is None:
        dir = os.getcwd()

    cmd = f"./{exe}"
    stdout, stderr, flag = run_cmd(cmd, dir)
    write_to_file(logfile, stdout + f"\n \n" + stderr)

    if flag != 0:
        if "ThreadSanitizer: data race" not in stderr:
            logger.error(f"error while running {exe}, we will not continue execution")
            return returnCode.ERROR
    
    return returnCode.SUCCESS


def run_dir(args) -> returnCode:
    exe_dir = args.dir
    current_dir = os.getcwd()
    data_time_dir = os.path.join(dataDir, time)
    alljson_dir = os.path.join(data_time_dir, "jsons")
    os.mkdir(data_time_dir)
    os.mkdir(alljson_dir)

    # run programs and output the result
    for filename in os.listdir(exe_dir):
        isexe = os.access(os.path.join(exe_dir, filename), os.X_OK)
        if not isexe or os.path.isdir(filename):
            continue
        output_dir = os.path.join(data_time_dir, filename)
        os.mkdir(output_dir)
        flag = run_exe(filename, runlogfile, exe_dir)

        if args.test:
            logger.info(f'Finished. Only a test, no JSON file created')
            continue

        # copy log files into output_dir
        cmd = f"cp {runlogfile} {movementlogfile} {rawgraphFile} {output_dir}"
        run_cmd(cmd, current_dir)

        if flag != returnCode.SUCCESS:
            continue

        # create json file in output_dir
        # jsonData = create_json()
        # if jsonData == "":
        #     continue
        # outputjson_path = os.path.join(output_dir, f'{filename}.json')
        # write_to_file(outputjson_path, jsonData)

        outputjson_path = os.path.join(output_dir, f'{filename}.json')
        cmd = f'cp {rawgraphFile} {outputjson_path}'
        run_cmd(cmd, current_dir)

        # only for debug purpose
        cmd = f"cp {outputjson_path} {alljson_dir}"
        run_cmd(cmd, current_dir)


    return returnCode.SUCCESS


def add_target_region_to_data(data):
    alltargetobj = []

    with open(movementlogfile, newline='') as csvfile:
        spamreader = csv.reader(csvfile, delimiter=',')

        targetobj = {}
        datamoveobj = []

        for row in spamreader:
            if row[0] != "begin_node" and row[0] != "orig_addr":
                datamoveobj.append({"orig_address": row[0], "dest_address":row[1], "bytes":row[2], "flag":row[3]})
            elif row[0] == "begin_node":
                if targetobj:
                    targetobj["datamove"] = datamoveobj
                    alltargetobj.append(targetobj)

                targetobj = {}
                datamoveobj = []
                targetobj["begin_node"] = row[1]
                targetobj["end_node"] = row[-1]

        # after the for loop finish
        if targetobj:
            targetobj["datamove"] = datamoveobj
            alltargetobj.append(targetobj)
            
    data["target"] = alltargetobj
    return


def create_json() -> str:
    G = None

    try:
        G = nx.read_graphml(rawgraphFile)
    except Exception as e:
        return ""

    # Find and remove nodes with attribute "vertex_id" equal to 0
    nodes_to_remove = [node for node, data in G.nodes(data=True) if data.get('vertex_id') == 0]
    G.remove_nodes_from(nodes_to_remove)

    graph_data = json_graph.node_link_data(G)

    for node in graph_data['nodes']:
        node["active"] = True
        node["hidden"] = False

    for edge in graph_data['links']:
        edge["hidden"] = False

    add_target_region_to_data(graph_data)
    jsonData = json.dumps(graph_data, indent=4)
    
    return jsonData


def main(args):
    if check_path_exists(args) != returnCode.SUCCESS:
        return

    if args.dir is not None:
        flag = run_dir(args)
        return
        
    if args.exe is not None:
        flag = run_exe(args.exe, runlogfile)
        if flag != returnCode.SUCCESS:
            return

    if args.test:
        logger.info(f'Finished. Only a test, no JSON file created')
        return

    # jsonData = create_json()

    outputname = f'output{time}.json'
    if args.exe is not None:
        binaryname = (args).exe.split("/")[-1]
        outputname = f'{binaryname}-{time}.json'
    outputpath = os.path.join(dataDir, outputname)
    cmd = f'cp {rawgraphFile} {outputpath}'
    run_cmd(cmd, os.getcwd())

    # write_to_file(outputpath, jsonData)
    logger.info(f'Conversion completed. JSON file saved as {outputpath}')
        


if __name__ == "__main__":

    parser = argparse.ArgumentParser(description="OpenMP run&vis program")
    parser.add_argument("--rawgraphFile", help="input file that contains the raw graphml data", type=str, default="rawgraphml.json")
    parser.add_argument("--exe", help="the OpenMP program to run", type=str)
    parser.add_argument("--dir", help="the directory which we will execute all OpenMP programs in it", type=str)

    parser.add_argument('--test', action='store_true', help="mark this run as a test, no output json file created.")
    parser.set_defaults(test=False)

    args = parser.parse_args()

    main(args)
# -*- coding: utf-8 -*-

"""
shepherd_herd
~~~~~
click-based command line utility for controlling a group of shepherd nodes
remotely through ssh. Provides commands for starting/stopping harvester and
emulator, retrieving recordings to the local machine and flashing firmware
images to target sensor nodes.

:copyright: (c) 2019 Networked Embedded Systems Lab, TU Dresden.
:license: MIT, see LICENSE for more details.
"""

import click
import re
import time
from fabric import Group
import numpy as np
from io import StringIO
from pathlib import Path
import telnetlib
import yaml
import logging
import click_config_file

consoleHandler = logging.StreamHandler()
logger = logging.getLogger("shepherd-herd")
logger.addHandler(consoleHandler)


def yamlprovider(file_path, cmd_name):
    logger.info(f"reading config from {file_path}")
    with open(file_path, "r") as config_data:
        full_config = yaml.safe_load(config_data)
    return full_config


def find_consensus_time(group):
    """Finds a start time in the future when all nodes should start service

    In order to run synchronously, all nodes should start at the same time.
    This is achieved by querying all nodes to check any large time offset,
    agreeing on a common time in the future and waiting for that time on each
    node.

    Args:
        group (fabric.Group): Group of fabric hosts on which to start shepherd.
    """
    # Get the current time on each target node
    ts_nows = np.empty(len(group))
    for i, cnx in enumerate(group):
        res = cnx.run("date +%s", hide=True, warn=True)
        ts_nows[i] = float(res.stdout)

    if len(ts_nows) == 1:
        ts_start = ts_nows[0] + 20
    else:
        ts_max = max(ts_nows)
        # Check for excessive time difference among nodes
        ts_diffs = ts_nows - ts_max
        if any(abs(ts_diffs) > 10):
            raise Exception("Time difference between hosts greater 10s")

        # We need to estimate a future point in time such that all nodes are ready
        ts_start = ts_max + 20 + 2 * len(group)
    return int(ts_start), float(ts_start - ts_nows[0])


def configure_shepherd(
    group: Group,
    mode: str,
    parameters: dict,
    hostnames: dict,
    verbose: int = 0,
):
    """Configures shepherd service on the group of hosts.

    Rolls out a configuration file according to the given command and parameters
    service.

    Args:
        group (fabric.Group): Group of fabric hosts on which to start shepherd.
        mode (str): What shepherd is supposed to do. One of 'harvester' or 'emulator'.
        parameters (dict): Parameters for shepherd-sheep
        hostnames (dict): Dictionary of hostnames corresponding to fabric hosts
        verbose (int): Verbosity for shepherd-sheep
    """
    config_dict = {
        "mode": mode,
        "verbose": verbose,
        "parameters": parameters,
    }
    config_yml = yaml.dump(
        config_dict, default_flow_style=False, sort_keys=False
    )
    
    logger.debug(f"Rolling out the following config:\n\n{config_yml}")

    for cnx in group:
        res = cnx.sudo("systemctl status shepherd", hide=True, warn=True)
        if res.exited != 3:
            raise Exception(f"shepherd not inactive on {hostnames[cnx.host]}")

        cnx.put(StringIO(config_yml), "/tmp/config.yml")
        cnx.sudo("mv /tmp/config.yml /etc/shepherd/config.yml")


def start_shepherd(
    group: Group, hostnames: dict,
):
    """Starts shepherd service on the group of hosts.

    Args:
        group (fabric.Group): Group of fabric hosts on which to start shepherd.
        hostnames (dict): Dictionary of hostnames corresponding to fabric hosts
    """
    for cnx in group:
        res = cnx.sudo("systemctl status shepherd", hide=True, warn=True)
        if res.exited != 3:
            raise Exception(f"shepherd not inactive on {hostnames[cnx.host]}")
        res = cnx.sudo("systemctl start shepherd", hide=True, warn=True)


@click.group(context_settings=dict(help_option_names=["-h", "--help"], obj={}))
@click.option("--inventory", "-i", type=str,
    default="inventory/herd.yml",
    help="List of target hosts as comma-separated string or path to ansible-style yaml file")
@click.option("--limit", "-l", type=str,
    help="Comma-separated list of hosts to limit execution to")
@click.option("--user", "-u", type=str, help="User name for login to nodes")
@click.option("--key-filename", "-k", type=click.Path(exists=True),
    help="Path to private ssh key file")
@click.option("-v", "--verbose", count=True, default=2)
@click.pass_context
def cli(ctx, inventory, limit, user, key_filename, verbose):

    if inventory.rstrip().endswith(","):
        hostlist = inventory.split(",")[:-1]
        if limit is not None:
            hostlist = list(set(hostlist) & set(limit))
        hostnames = {hostname: hostname for hostname in hostlist}

    else:
        host_path = Path(inventory)
        if not host_path.exists():
            raise click.FileError(inventory)

        with open(host_path, "r") as stream:
            try:
                inventory_data = yaml.safe_load(stream)
            except yaml.YAMLError:
                raise click.UsageError(
                    f"Couldn't read inventory file {host_path}"
                )

        hostlist = list()
        hostnames = dict()
        for hostname, hostvars in inventory_data["sheep"]["hosts"].items():
            if limit is not None:
                if not hostname in limit.split(","):
                    continue

            if "ansible_host" in hostvars.keys():
                hostlist.append(hostvars["ansible_host"])
                hostnames[hostvars["ansible_host"]] = hostname
            else:
                hostlist.append(hostname)
                hostnames[hostname] = hostname

        if user is None:
            try:
                user = inventory_data["sheep"]["vars"]["ansible_user"]
            except KeyError:
                pass

    if user is None:
        raise click.UsageError(
            "Provide user by command line or in inventory file"
        )

    if verbose == 0:
        logger.setLevel(logging.ERROR)
    elif verbose == 1:
        logger.setLevel(logging.WARNING)
    elif verbose == 2:
        logger.setLevel(logging.INFO)
    elif verbose > 2:
        logger.setLevel(logging.DEBUG)

    ctx.obj["verbose"] = verbose

    connect_kwargs = dict()
    if key_filename is not None:
        connect_kwargs["key_filename"] = key_filename

    ctx.obj["fab group"] = Group(
        *hostlist, user=user, connect_kwargs=connect_kwargs
    )
    ctx.obj["hostnames"] = hostnames


@cli.command(short_help="Power off shepherd nodes")
@click.option("--restart", "-r", is_flag=True, help="Reboot")
@click.pass_context
def poweroff(ctx, restart):
    for cnx in ctx.obj["fab group"]:
        if restart:
            logger.info(f"rebooting {ctx.obj['hostnames'][cnx.host]}")
            cnx.sudo("reboot", hide=True, warn=True)
        else:
            logger.info(f"powering off {ctx.obj['hostnames'][cnx.host]}")
            cnx.sudo("poweroff", hide=True, warn=True)


@cli.command(short_help="Run COMMAND on the shell")
@click.pass_context
@click.argument("command", type=str)
@click.option("--sudo", "-s", is_flag=True, help="Run command with sudo")
def run(ctx, command, sudo):
    for cnx in ctx.obj["fab group"]:
        click.echo(
            f"************** {ctx.obj['hostnames'][cnx.host]} **************"
        )
        if sudo:
            cnx.sudo(command, warn=True)
        else:
            cnx.run(command, warn=True)


@cli.group(
    short_help="Remote programming/debugging of the target sensor node",
    invoke_without_command=True,
)
@click.option(
    "--port",
    "-p",
    type=int,
    default=4444,
    help="Port on which OpenOCD should listen for telnet",
)
@click.option(
    "--on/--off",
    default=True,
    help="Enable/disable power and debug access to the target",
)
@click.option("--voltage", "-v", type=float, default=3.0, help="Target supply voltage")
@click.option("--sel_a/--sel_b", default=True,
              help="Choose (main)Target that gets connected to virtual Source")
@click.pass_context
def target(ctx, port, on, voltage, sel_a):
    ctx.obj["openocd_telnet_port"] = port
    sel_target = "sel_a" if sel_a else "sel_b"
    if on or ctx.invoked_subcommand:
        for cnx in ctx.obj["fab group"]:
            cnx.sudo(f"shepherd-sheep target-power --on --voltage {voltage} --{sel_target}", hide=True)
            start_openocd(cnx, ctx.obj["hostnames"][cnx.host])
    else:
        for cnx in ctx.obj["fab group"]:
            cnx.sudo("systemctl stop shepherd-openocd")
            cnx.sudo("shepherd-sheep target-power --off", hide=True)


@target.resultcallback()
@click.pass_context
def process_result(ctx, result, **kwargs):
    if not kwargs["on"]:
        for cnx in ctx.obj["fab group"]:
            cnx.sudo("systemctl stop shepherd-openocd")
            cnx.sudo("shepherd-sheep target-power --off", hide=True)


def start_openocd(cnx, hostname, timeout=30):
    cnx.sudo("systemctl start shepherd-openocd", hide=True, warn=True)
    ts_end = time.time() + timeout
    while True:
        openocd_status = cnx.sudo(
            "systemctl status shepherd-openocd", hide=True, warn=True
        )
        if openocd_status.exited == 0:
            break
        if time.time() > ts_end:
            raise TimeoutError(
                f"Timed out waiting for openocd on host {hostname}"
            )
        else:
            logger.debug(f"waiting for openocd on {hostname}")
            time.sleep(1)


@target.command(short_help="Flashes the binary IMAGE file to the target")
@click.argument("image", type=click.Path(exists=True))
@click.pass_context
def flash(ctx, image):
    for cnx in ctx.obj["fab group"]:
        cnx.put(image, "/tmp/target_image.bin")

        with telnetlib.Telnet(cnx.host, ctx.obj["openocd_telnet_port"]) as tn:
            logger.debug(
                f"connected to openocd on {ctx.obj['hostnames'][cnx.host]}"
            )
            tn.write(b"program /tmp/target_image.bin verify reset\n")
            res = tn.read_until(b"Verified OK", timeout=5)
            if b"Verified OK" in res:
                logger.info(
                    f"flashed image on {ctx.obj['hostnames'][cnx.host]} successfully"
                )
            else:
                logger.error(
                    f"failed flashing image on {ctx.obj['hostnames'][cnx.host]}"
                )


@target.command(short_help="Halts the target")
@click.pass_context
def halt(ctx):
    for cnx in ctx.obj["fab group"]:

        with telnetlib.Telnet(cnx.host, ctx.obj["openocd_telnet_port"]) as tn:
            logger.debug(
                f"connected to openocd on {ctx.obj['hostnames'][cnx.host]}"
            )
            tn.write(b"halt\n")
            logger.info(f"target halted on {ctx.obj['hostnames'][cnx.host]}")


@target.command(short_help="Erases the target")
@click.pass_context
def erase(ctx):
    for cnx in ctx.obj["fab group"]:

        with telnetlib.Telnet(cnx.host, ctx.obj["openocd_telnet_port"]) as tn:
            logger.debug(
                f"connected to openocd on {ctx.obj['hostnames'][cnx.host]}"
            )
            tn.write(b"halt\n")
            logger.info(f"target halted on {ctx.obj['hostnames'][cnx.host]}")
            tn.write(b"nrf52 mass_erase\n")
            logger.info(f"target erased on {ctx.obj['hostnames'][cnx.host]}")


@target.command(short_help="Resets the target")
@click.pass_context
def reset(ctx):
    for cnx in ctx.obj["fab group"]:

        with telnetlib.Telnet(cnx.host, ctx.obj["openocd_telnet_port"]) as tn:
            logger.debug(
                f"connected to openocd on {ctx.obj['hostnames'][cnx.host]}"
            )
            tn.write(b"reset\n")
            logger.info(f"target reset on {ctx.obj['hostnames'][cnx.host]}")


@cli.command(short_help="Record IV data from a harvest-source")
@click.option("--output_path", "-o", type=click.Path(),
    default="/var/shepherd/recordings/",
    help="Dir or file path for resulting hdf5 file")
@click.option("--algorithm", "-a", type=str, default=None,
              help="Choose one of the predefined virtual harvesters")
@click.option("--duration", "-d", type=click.FLOAT, help="Duration of recording in seconds")
@click.option("--force_overwrite", "-f", is_flag=True, help="Overwrite existing file")
@click.option("--use_cal_default", is_flag=True, help="Use default calibration values")
@click.option("--start/--no-start", default=True, help="Start shepherd after uploading config")
@click.pass_context
def harvester(
    ctx,
    output_path,
    algorithm,
    duration,
    force_overwrite,
    use_cal_default,
    start,
):
    fp_output = Path(output_path)
    if not fp_output.is_absolute():
        fp_output = Path("/var/shepherd/recordings") / output_path

    parameter_dict = {
        "output_path": str(fp_output),
        "harvester": algorithm,
        "duration": duration,
        "force_overwrite": force_overwrite,
        "use_cal_default": use_cal_default,
    }
    
    if start:
        ts_start, delay = find_consensus_time(ctx.obj["fab group"])
        parameter_dict["start_time"] = ts_start
    
    configure_shepherd(
        ctx.obj["fab group"],
        "harvester",
        parameter_dict,
        ctx.obj["hostnames"],
        ctx.obj["verbose"],
    )
    
    if start:
        logger.debug(f"Scheduling start of shepherd at {ts_start} (in ~ {delay} s)")
        start_shepherd(ctx.obj["fab group"], ctx.obj["hostnames"])


@cli.command(short_help="Emulate data, where INPUT is an hdf5 file containing harvesting data")
@click.argument("input_path", type=click.Path())
@click.option("--output_path", "-o", type=click.Path(),
    default="/var/shepherd/recordings/",
    help="Dir or file path for resulting hdf5 file with load recordings")
@click.option("--duration", "-d", type=click.FLOAT, help="Duration of recording in seconds")
@click.option("--force_overwrite", "-f", is_flag=True, help="Overwrite existing file")
@click.option("--use_cal_default", is_flag=True, help="Use default calibration values")
@click.option("--enable_io/--disable_io", default=True,
              help="Switch the GPIO level converter to targets on/off")
@click.option("--io_sel_target_a/--io_sel_target_b", default=True,
              help="Choose Target that gets connected to IO")
@click.option("--pwr_sel_target_a/--pwr_sel_target_b", default=True,
              help="Choose (main)Target that gets connected to virtual Source")
@click.option("--aux_voltage", type=float,
              help="Set Voltage of auxiliary Power Source (second target)")
@click.option("--virtsource", default=dict(), help="Use the desired setting for the virtual source")
@click_config_file.configuration_option(provider=yamlprovider, implicit=False)
@click.option(
    "--start/--no-start",
    default=True,
    help="Start shepherd after uploading config",
)
@click.pass_context
def emulator(
    ctx,
    input_path,
    output_path,
    duration,
    force_overwrite,
    use_cal_default,
    enable_target_io,
    sel_target_a_for_io,
    sel_target_a_for_pwr,
    aux_target_voltage,
    virtsource,
    start,
):

    fp_input = Path(input_path)
    if not fp_input.is_absolute():
        fp_input = Path("/var/shepherd/recordings") / input_path

    parameter_dict = {
        "input_path": str(fp_input),
        "force_overwrite": force_overwrite,
        "duration": duration,
        "use_cal_default": use_cal_default,
        "set_target_io_lvl_conv": enable_target_io,
        "sel_target_for_io": sel_target_a_for_io,
        "sel_target_for_pwr": sel_target_a_for_pwr,
        "aux_target_voltage": aux_target_voltage,
        "settings_virtsource": virtsource,
    }

    if output_path is not None:
        fp_output = Path(output_path)
        if not fp_output.is_absolute():
            fp_output = Path("/var/shepherd/recordings") / output_path

        parameter_dict["output_path"] = str(fp_output)

    if start:
        ts_start, delay = find_consensus_time(ctx.obj["fab group"])
        parameter_dict["start_time"] = ts_start

    configure_shepherd(
        ctx.obj["fab group"],
        "emulator",
        parameter_dict,
        ctx.obj["hostnames"],
        ctx.obj["verbose"],
    )
    
    if start:
        logger.debug(f"Scheduling start of shepherd at {ts_start} (in ~ {delay} s)")
        start_shepherd(ctx.obj["fab group"], ctx.obj["hostnames"])


@cli.command(short_help="Stops any harvest/emulation")
@click.pass_context
def stop(ctx):
    for cnx in ctx.obj["fab group"]:
        cnx.sudo("systemctl stop shepherd", hide=True, warn=True)


@cli.command(
    short_help="Retrieves remote hdf file FILENAME and stores in in OUTDIR"
)
@click.argument("filename", type=click.Path())
@click.argument("outdir", type=click.Path(exists=True))
@click.option("--rename", "-r", is_flag=True, 
    help="Add current timestamp to measurement file")
@click.option("--delete", "-d", is_flag=True,
    help="Delete the file from the remote filesystem after retrieval")
@click.option("--stop", "-s", is_flag=True,
    help="Stop the on-going harvest/emulation process before retrieving the data",)
@click.pass_context
def retrieve(ctx, filename, outdir, rename, delete, stop):
    if stop:
        for cnx in ctx.obj["fab group"]:

            logger.info(
                f"stopping shepherd service on {ctx.obj['hostnames'][cnx.host]}"
            )
            res = cnx.sudo("systemctl stop shepherd", hide=True, warn=True)

    time_str = time.strftime("%Y_%m_%dT%H_%M_%S")
    ts_end = time.time() + 30
    for cnx in ctx.obj["fab group"]:
        while True:
            res = cnx.sudo("systemctl status shepherd", hide=True, warn=True)
            if res.exited == 3:
                break
            if not stop or time.time() > ts_end:
                raise Exception(
                    f"shepherd not inactive on {ctx.obj['hostnames'][cnx.host]}"
                )
            time.sleep(1)

        target_path = Path(outdir) / ctx.obj["hostnames"][cnx.host]
        if not target_path.exists():
            logger.info(f"creating local dir {target_path}")
            target_path.mkdir()

        if Path(filename).is_absolute():
            filepath = Path(filename)
        else:
            filepath = Path("/var/shepherd/recordings") / filename

        if rename:
            local_path = target_path / f"rec_{ time_str }.h5"
        else:
            local_path = target_path / filepath.name

        logger.info(
            (
                f"retrieving remote file {filepath} from "
                f"{ctx.obj['hostnames'][cnx.host]} to local {local_path}"
            )
        )
        cnx.get(filepath, local=Path(local_path))
        if delete:
            logger.info(
                f"deleting {filepath} from remote {ctx.obj['hostnames'][cnx.host]}"
            )
            cnx.sudo(f"rm {str(filepath)}", hide=True)


def main():
    return cli(obj={})

import pytest
import numpy as np
import time
import logging
from pathlib import Path
import h5py
from itertools import product

from shepherd import LogReader, cal_channel_list
from shepherd import LogWriter
from shepherd import CalibrationData
from shepherd.calibration import cal_parameter_list, cal_channel_harvest_dict

from shepherd.shepherd_io import DataBuffer
from shepherd.shepherd_io import GPIOEdges
from shepherd.datalog import ExceptionRecord


def random_data(length):
    return np.random.randint(0, high=2 ** 18, size=length, dtype="u4")


@pytest.fixture()
def data_buffer():
    len_ = 10_000
    voltage = random_data(len_)
    current = random_data(len_)
    data = DataBuffer(voltage, current, 1551848387472)
    return data


@pytest.fixture
def data_h5(tmp_path):
    name = tmp_path / "record_example.h5"
    with LogWriter(name, CalibrationData.from_default()) as store:
        for i in range(100):
            len_ = 10_000
            fake_data = DataBuffer(random_data(len_), random_data(len_), i)
            store.write_buffer(fake_data)
    return name


@pytest.fixture
def calibration_data():
    cd = CalibrationData.from_default()
    return cd


@pytest.mark.parametrize("mode", ["harvester"])
def test_create_logwriter(mode, tmp_path, calibration_data):
    d = tmp_path / f"{ mode }.h5"
    h = LogWriter(store_path=d, calibration_data=calibration_data, mode=mode)
    assert not d.exists()
    h.__enter__()
    assert d.exists()
    h.__exit__()


def test_create_logwriter_with_force(tmp_path, calibration_data):
    d = tmp_path / "harvest.h5"
    d.touch()
    stat = d.stat()
    time.sleep(0.1)

    h = LogWriter(store_path=d, calibration_data=calibration_data, force_overwrite=False)
    h.__enter__()
    h.__exit__()
    # This should have created the following alternative file:
    d_altered = tmp_path / "harvest.0.h5"
    assert h.store_path == d_altered
    assert d_altered.exists()

    h = LogWriter(store_path=d, calibration_data=calibration_data, force_overwrite=True)
    h.__enter__()
    h.__exit__()
    new_stat = d.stat()
    assert new_stat.st_mtime > stat.st_mtime


@pytest.mark.parametrize("mode", ["harvester"])
def test_logwriter_data(mode, tmp_path, data_buffer, calibration_data):
    d = tmp_path / "harvest.h5"
    with LogWriter(
        store_path=d, calibration_data=calibration_data, mode=mode
    ) as log:
        log.write_buffer(data_buffer)

    with h5py.File(d, "r") as written:

        assert "data" in written
        assert "time" in written["data"]
        for variable in ["voltage", "current"]:
            assert variable in written["data"]  # .keys()
            ref_var = getattr(data_buffer, variable)
            assert all(written["data"][variable][:] == ref_var)


@pytest.mark.parametrize("mode", ["harvester"])
def test_calibration_logging(mode, tmp_path, calibration_data):
    d = tmp_path / "recording.h5"
    with LogWriter(
        store_path=d, mode=mode, calibration_data=calibration_data
    ) as _:
        pass

    h5store = h5py.File(d, "r")  # hint: logReader would be more direct, but less untouched

    for channel_entry, parameter in product(cal_channel_harvest_dict.items(), cal_parameter_list):
        assert (
            h5store["data"][channel_entry[0]].attrs[parameter]
            == calibration_data[mode][channel_entry[1]][parameter]
        )


def test_exception_logging(tmp_path, data_buffer, calibration_data):
    d = tmp_path / "harvest.h5"

    with LogWriter(store_path=d, calibration_data=calibration_data) as writer:
        writer.write_buffer(data_buffer)

        ts = int(time.time() * 1000)
        writer.write_exception(ExceptionRecord(ts, "there was an exception", 0))
        writer.write_exception(
            ExceptionRecord(ts + 1, "there was another exception", 1)
        )

        # Note: decode is needed at least for h5py < 3, and old dtype=h5py.special_dtype(vlen=str)
        assert writer.xcpt_grp["message"][0].decode("UTF8") == "there was an exception"
        assert writer.xcpt_grp["message"][1].decode("UTF8") == "there was another exception"
        assert writer.xcpt_grp["value"][0] == 0
        assert writer.xcpt_grp["value"][1] == 1
        assert writer.xcpt_grp["time"][0] == ts
        assert writer.xcpt_grp["time"][1] == ts + 1


def test_key_value_store(tmp_path, calibration_data):
    d = tmp_path / "harvest.h5"

    with LogWriter(store_path=d, calibration_data=calibration_data) as writer:

        writer["some string"] = "this is a string"
        writer["some value"] = 5

    with h5py.File(d, "r+") as hf:
        assert hf.attrs["some value"] == 5
        assert hf.attrs["some string"] == "this is a string"


@pytest.mark.timeout(2)
def test_logwriter_performance(tmp_path, data_buffer, calibration_data):
    d = tmp_path / "harvest_perf.h5"
    with LogWriter(store_path=d, force_overwrite=True, calibration_data=calibration_data) as log:
        log.write_buffer(data_buffer)


def test_logreader_performance(data_h5):
    read_durations = []
    with LogReader(store_path=data_h5, samples_per_buffer=10_000) as reader:
        past = time.time()
        for data in reader.read_buffers():
            now = time.time()
            elapsed = now - past
            read_durations.append(elapsed)
            past = time.time()
    assert np.mean(read_durations) < 0.05

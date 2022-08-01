"""

this is ported py-version of the pru-code, goals:
- stay close to original code-base
- offer a comparison for the tests
- step 1 to a virtualization of emulation

NOTE: DO NOT OPTIMIZE -> stay close to original code-base

"""
from typing import Union

from shepherd import CalibrationData, VirtualSourceConfig, VirtualHarvesterConfig
from shepherd.virtual_converter_model import PruCalibration, VirtualConverterModel, KernelConverterStruct
from shepherd.virtual_harvester_model import VirtualHarvesterModel, KernelHarvesterStruct


class VirtualSourceModel:
    """part of sampling.c"""

    _cal: CalibrationData = None
    _prc: PruCalibration = None
    hrv: VirtualHarvesterModel = None
    cnv: VirtualConverterModel = None

    def __init__(self, vs_setting: Union[dict, VirtualSourceConfig], cal_data: CalibrationData, input_setting: Union[None, dict]):

        self._cal = cal_data
        self._prc = PruCalibration(cal_data)

        vs_config = VirtualSourceConfig(vs_setting)
        vc_struct = KernelConverterStruct(vs_config)
        self.cnv = VirtualConverterModel(vc_struct, self._prc)

        vh_config = VirtualHarvesterConfig(vs_config.get_harvester(), vs_config.samplerate_sps, emu_cfg=input_setting)
        vh_struct = KernelHarvesterStruct(vh_config)
        self.hrv = VirtualHarvesterModel(vh_struct)

    def iterate_sampling(self, V_inp_uV: int = 0, I_inp_nA: int = 0, A_out_nA: int = 0):
        """ TEST-SIMPLIFICATION - code below is not part of pru-code, but in part sample_emulator() in sampling.c

        :param V_inp_uV:
        :param I_inp_nA:
        :param A_out_nA:
        :return:
        """
        V_inp_uV, I_inp_nA = self.hrv.iv_sample(V_inp_uV, I_inp_nA)

        self.cnv.calc_inp_power(V_inp_uV, I_inp_nA)

        # fake ADC read
        A_out_raw = self._cal.convert_value_to_raw(
            "emulator", "adc_current", A_out_nA * 10**-9
        )

        self.cnv.calc_out_power(A_out_raw)
        self.cnv.update_cap_storage()
        V_out_raw = self.cnv.update_states_and_output()
        V_out_uV = int(
            self._cal.convert_raw_to_value("emulator", "dac_voltage_b", V_out_raw)
            * 10**6
        )
        self.cnv.P_inp_fW += V_inp_uV * I_inp_nA
        self.cnv.P_out_fW += V_out_uV * A_out_nA
        return V_out_uV

// zZtop -- minimal two-probe FRA front-end (first pass)
//
// The job of this stage is to decouple the codec/DDS output impedance from the
// system under test and measure cell current. It's the classic Bard adder
// potentiostat simplified to two electrodes:
//
//   VEXC --> [U_PC driver/buffer] --DRIVE--> ( R||C cell ) --WE--> [U_CF TIA] --> VOUT
//
//   U_PC : unity-gain buffer, low output-Z drive (decouples the codec)
//   U_CF : transimpedance amp, WE held at virtual ground, VOUT = -i * RCF
//
// First pass is for topology + SPICE sanity. Real DIP parts (296-12221 quad,
// AD820 on the CF) and R_CF ranges / comp caps get mapped in from the olm-pstat
// Eagle schematic next.

export default () => (
  <board routingDisabled>
    {/* Excitation: the codec / DDS drive, modeled as a sine source */}
    <voltagesource name="VEXC" waveShape="sinewave" voltage="0.1V" frequency="1kHz" />

    {/* Op-amp supply rails (pin4 = V+, pin5 = V-) */}
    <voltagesource name="VPLUS" voltage="5V" />
    <voltagesource name="VMINUS" voltage="-5V" />

    {/* PC / driver amp: unity-gain buffer, presents a low output impedance to the
        cell so the codec's own output impedance does not appear in the measurement */}
    <opamp
      name="U_PC"
      connections={{
        non_inverting_input: "net.VEXC",
        inverting_input: "net.DRIVE",
        output: "net.DRIVE",
      }}
    />

    {/* Cell under test, two-electrode R||C: DRIVE electrode -> WE electrode */}
    <resistor name="RCELL" resistance="4.7k" />
    <capacitor name="CCELL" capacitance="100nF" />

    {/* CF / transimpedance amp: WE held at virtual ground, VOUT = -i * RCF */}
    <resistor name="RCF" resistance="10k" />
    <opamp
      name="U_CF"
      connections={{
        non_inverting_input: "net.GND",
        inverting_input: "net.WE",
        output: "net.VOUT",
      }}
    />

    {/* Wiring */}
    <trace from=".VEXC > .pin1" to="net.VEXC" />
    <trace from=".VEXC > .pin2" to="net.GND" />

    {/* supply rails */}
    <trace from=".VPLUS > .pin1" to="net.VPLUS" />
    <trace from=".VPLUS > .pin2" to="net.GND" />
    <trace from=".VMINUS > .pin1" to="net.VMINUS" />
    <trace from=".VMINUS > .pin2" to="net.GND" />
    <trace from=".U_PC > .pin4" to="net.VPLUS" />
    <trace from=".U_PC > .pin5" to="net.VMINUS" />
    <trace from=".U_CF > .pin4" to="net.VPLUS" />
    <trace from=".U_CF > .pin5" to="net.VMINUS" />

    <trace from="net.DRIVE" to=".RCELL > .pin1" />
    <trace from="net.DRIVE" to=".CCELL > .pin1" />
    <trace from=".RCELL > .pin2" to="net.WE" />
    <trace from=".CCELL > .pin2" to="net.WE" />

    <trace from="net.WE" to=".RCF > .pin1" />
    <trace from=".RCF > .pin2" to="net.VOUT" />

    {/* Observation: cell-drive node in, transimpedance out */}
    <voltageprobe name="VP_DRIVE" connectsTo=".RCELL > .pin1" />
    <voltageprobe name="VP_OUT" connectsTo=".RCF > .pin2" />

    <analogsimulation duration="5ms" timePerStep="5us" spiceEngine="ngspice" />
  </board>
)

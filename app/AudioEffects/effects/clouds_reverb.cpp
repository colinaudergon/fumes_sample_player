#include "clouds_reverb.h"

uint16_t reverb_buffer[16384];

int app::audio::CloudsReverb::Init()
{
    engine_.Init(reverb_buffer);
    engine_.SetLFOFrequency(LFO_1, 0.5f / 32000.0f);
    engine_.SetLFOFrequency(LFO_2, 0.3f / 32000.0f);
    low_pass_.value = 0.7f;
    diffusion_.value = 0.625f;
    return 0;
}

void app::audio::CloudsReverb::Process(float *input_l, float *input_r, float *output_l, float *output_r, size_t n_frames)
{

    // This is the Griesinger topology described in the Dattorro paper
    // (4 AP diffusers on the input, then a loop of 2x 2AP+1Delay).
    // Modulation is applied in the loop of the first diffuser AP for additional
    // smearing; and to the two long delays for a slow shimmer/chorus effect.
    typedef E::Reserve<113,
                       E::Reserve<162,
                                  E::Reserve<241,
                                             E::Reserve<399,
                                                        E::Reserve<1653,
                                                                   E::Reserve<2038,
                                                                              E::Reserve<3411,
                                                                                         E::Reserve<1913,
                                                                                                    E::Reserve<1663,
                                                                                                               E::Reserve<4782>>>>>>>>>>
        Memory;
    E::DelayLine<Memory, 0> ap1;
    E::DelayLine<Memory, 1> ap2;
    E::DelayLine<Memory, 2> ap3;
    E::DelayLine<Memory, 3> ap4;
    E::DelayLine<Memory, 4> dap1a;
    E::DelayLine<Memory, 5> dap1b;
    E::DelayLine<Memory, 6> del1;
    E::DelayLine<Memory, 7> dap2a;
    E::DelayLine<Memory, 8> dap2b;
    E::DelayLine<Memory, 9> del2;
    E::Context c;

    const float kap = diffusion_.value;
    const float klp = low_pass_.value;
    const float krt = time_.value;
    const float amount = amount_.value;
    const float gain = input_gain_.value;

    float lp_1 = lp_decay_1_;
    float lp_2 = lp_decay_2_;

    for (size_t i = 0; i < n_frames; i++)
    {
        float in_out_l = input_l[i];
        float in_out_r = input_r[i];
        float wet;
        float apout = 0.0f;
        engine_.Start(&c);

        // Smear AP1 inside the loop.
        c.Interpolate(ap1, 10.0f, LFO_1, 60.0f, 1.0f);
        c.Write(ap1, 100, 0.0f);

        c.Read(in_out_l + in_out_r, gain);

        // Diffuse through 4 allpasses.
        c.Read(ap1 TAIL, kap);
        c.WriteAllPass(ap1, -kap);
        c.Read(ap2 TAIL, kap);
        c.WriteAllPass(ap2, -kap);
        c.Read(ap3 TAIL, kap);
        c.WriteAllPass(ap3, -kap);
        c.Read(ap4 TAIL, kap);
        c.WriteAllPass(ap4, -kap);
        c.Write(apout);

        // Main reverb loop.
        c.Load(apout);
        c.Interpolate(del2, 4680.0f, LFO_2, 100.0f, krt);
        c.Lp(lp_1, klp);
        c.Read(dap1a TAIL, -kap);
        c.WriteAllPass(dap1a, kap);
        c.Read(dap1b TAIL, kap);
        c.WriteAllPass(dap1b, -kap);
        c.Write(del1, 2.0f);
        c.Write(wet, 0.0f);

        in_out_l += (wet - in_out_l) * amount;

        c.Load(apout);
        // c.Interpolate(del1, 4450.0f, LFO_1, 50.0f, krt);
        c.Read(del1 TAIL, krt);
        c.Lp(lp_2, klp);
        c.Read(dap2a TAIL, kap);
        c.WriteAllPass(dap2a, -kap);
        c.Read(dap2b TAIL, -kap);
        c.WriteAllPass(dap2b, kap);
        c.Write(del2, 2.0f);
        c.Write(wet, 0.0f);

        in_out_r += (wet - in_out_r) * amount;
        output_l[i] = in_out_l;
        output_r[i] = in_out_r;
    }

    lp_decay_1_ = lp_1;
    lp_decay_2_ = lp_2;
}

int app::audio::CloudsReverb::UpdateParameter(size_t parameter_id, float parameter_value)
{
    return 0;
}

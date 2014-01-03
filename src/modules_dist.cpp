/* Calf DSP plugin pack
 * Distortion related plugins
 *
 * Copyright (C) 2001-2010 Krzysztof Foltman, Markus Schmidt, Thor Harald Johansen and others
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, 
 * Boston, MA  02110-1301  USA
 */
#include <limits.h>
#include <memory.h>
#include <calf/giface.h>
#include <calf/modules_dist.h>

using namespace dsp;
using namespace calf_plugins;

#define SET_IF_CONNECTED(name) if (params[AM::param_##name] != NULL) *params[AM::param_##name] = name;

/**********************************************************************
 * SATURATOR by Markus Schmidt
**********************************************************************/

saturator_audio_module::saturator_audio_module()
{
    is_active        = false;
    srate            = 0;
    meter_drive      = 0.f;
    lp_pre_freq_old  = -1;
    hp_pre_freq_old  = -1;
    lp_post_freq_old = -1;
    hp_post_freq_old = -1;
    p_freq_old       = -1;
    p_level_old      = -1;
    p_q_old          = -1;
}

void saturator_audio_module::activate()
{
    is_active = true;
    // set all filters
    params_changed();
    meter_drive = 0.f;
}
void saturator_audio_module::deactivate()
{
    is_active = false;
}

void saturator_audio_module::params_changed()
{
    // set the params of all filters
    if(*params[param_lp_pre_freq] != lp_pre_freq_old) {
        lp[0][0].set_lp_rbj(*params[param_lp_pre_freq], 0.707, (float)srate);
        if(in_count > 1 && out_count > 1)
            lp[1][0].copy_coeffs(lp[0][0]);
        lp[0][1].copy_coeffs(lp[0][0]);
        if(in_count > 1 && out_count > 1)
            lp[1][1].copy_coeffs(lp[0][0]);
        lp_pre_freq_old = *params[param_lp_pre_freq];
    }
    if(*params[param_hp_pre_freq] != hp_pre_freq_old) {
        hp[0][0].set_hp_rbj(*params[param_hp_pre_freq], 0.707, (float)srate);
        if(in_count > 1 && out_count > 1)
            hp[1][0].copy_coeffs(hp[0][0]);
        hp[0][1].copy_coeffs(hp[0][0]);
        if(in_count > 1 && out_count > 1)
            hp[1][1].copy_coeffs(hp[0][0]);
        hp_pre_freq_old = *params[param_hp_pre_freq];
    }
    if(*params[param_lp_post_freq] != lp_post_freq_old) {
        lp[0][2].set_lp_rbj(*params[param_lp_post_freq], 0.707, (float)srate);
        if(in_count > 1 && out_count > 1)
            lp[1][2].copy_coeffs(lp[0][2]);
        lp[0][3].copy_coeffs(lp[0][2]);
        if(in_count > 1 && out_count > 1)
            lp[1][3].copy_coeffs(lp[0][2]);
        lp_post_freq_old = *params[param_lp_post_freq];
    }
    if(*params[param_hp_post_freq] != hp_post_freq_old) {
        hp[0][2].set_hp_rbj(*params[param_hp_post_freq], 0.707, (float)srate);
        if(in_count > 1 && out_count > 1)
            hp[1][2].copy_coeffs(hp[0][2]);
        hp[0][3].copy_coeffs(hp[0][2]);
        if(in_count > 1 && out_count > 1)
            hp[1][3].copy_coeffs(hp[0][2]);
        hp_post_freq_old = *params[param_hp_post_freq];
    }
    if(*params[param_p_freq] != p_freq_old or *params[param_p_level] != p_level_old or *params[param_p_q] != p_q_old) {
        p[0].set_peakeq_rbj((float)*params[param_p_freq], (float)*params[param_p_q], (float)*params[param_p_level], (float)srate);
        if(in_count > 1 && out_count > 1)
            p[1].copy_coeffs(p[0]);
        p_freq_old = *params[param_p_freq];
        p_level_old = *params[param_p_level];
        p_q_old = *params[param_p_q];
    }
    // set distortion
    dist[0].set_params(*params[param_blend], *params[param_drive]);
    if(in_count > 1 && out_count > 1)
        dist[1].set_params(*params[param_blend], *params[param_drive]);
}

void saturator_audio_module::set_sample_rate(uint32_t sr)
{
    srate = sr;
    dist[0].set_sample_rate(sr);
    if(in_count > 1 && out_count > 1)
        dist[1].set_sample_rate(sr);
    int meter[] = {param_meter_in,  param_meter_out, param_meter_drive};
    int clip[] = {param_clip_in, param_clip_out, -1};
    meters.init(params, meter, clip, 3, srate);
}

uint32_t saturator_audio_module::process(uint32_t offset, uint32_t numsamples, uint32_t inputs_mask, uint32_t outputs_mask)
{
    bool bypass = *params[param_bypass] > 0.5f;
    numsamples += offset;
    if(bypass) {
        // everything bypassed
        while(offset < numsamples) {
            if(in_count > 1 && out_count > 1) {
                outs[0][offset] = ins[0][offset];
                outs[1][offset] = ins[1][offset];
            } else if(in_count > 1) {
                outs[0][offset] = (ins[0][offset] + ins[1][offset]) / 2;
            } else if(out_count > 1) {
                outs[0][offset] = ins[0][offset];
                outs[1][offset] = ins[0][offset];
            } else {
                outs[0][offset] = ins[0][offset];
            }
            float values[] = {0, 0, 0};
            meters.process(values);
            ++offset;
        }
    } else {
        // process
        while(offset < numsamples) {
            // cycle through samples
            float out[2], in[2] = {0.f, 0.f};
            int c = 0;
            float maxDrive = 0.f;
            if(in_count > 1 && out_count > 1) {
                // stereo in/stereo out
                // handle full stereo
                in[0] = ins[0][offset];
                in[1] = ins[1][offset];
                c = 2;
            } else {
                // in and/or out mono
                // handle mono
                in[0] = ins[0][offset];
                in[1] = in[0];
                c = 1;
            }
            
            float proc[2];
            proc[0] = in[0] * *params[param_level_in];
            proc[1] = in[1] * *params[param_level_in];
            
            float onedivlevelin = 1.0 / *params[param_level_in];
            
            for (int i = 0; i < c; ++i) {
                // all pre filters in chain
                proc[i] = lp[i][1].process(lp[i][0].process(proc[i]));
                proc[i] = hp[i][1].process(hp[i][0].process(proc[i]));

                // ...saturate...
                proc[i] = dist[i].process(proc[i]);
                
                // tone control
                proc[i] = p[i].process(proc[i]);

                // all post filters in chain
                proc[i] = lp[i][2].process(lp[i][3].process(proc[i]));
                proc[i] = hp[i][2].process(hp[i][3].process(proc[i]));
                
                //subtract gain
                proc[i] *= onedivlevelin;
            }
            
            maxDrive = dist[0].get_distortion_level() * *params[param_blend];
            
            if(in_count > 1 && out_count > 1) {
                maxDrive = dist[1].get_distortion_level() * *params[param_blend];
                // full stereo
                out[0] = ((proc[0] * *params[param_mix]) + in[0] * (1 - *params[param_mix])) * *params[param_level_out];
                outs[0][offset] = out[0];
                out[1] = ((proc[1] * *params[param_mix]) + in[1] * (1 - *params[param_mix])) * *params[param_level_out];
                outs[1][offset] = out[1];
            } else if(out_count > 1) {
                // mono -> pseudo stereo
                out[0] = ((proc[0] * *params[param_mix]) + in[0] * (1 - *params[param_mix])) * *params[param_level_out];
                outs[0][offset] = out[0];
                out[1] = out[0];
                outs[1][offset] = out[1];
            } else {
                // stereo -> mono
                // or full mono
                out[0] = ((proc[0] * *params[param_mix]) + in[0] * (1 - *params[param_mix])) * *params[param_level_out];
                outs[0][offset] = out[0];
            }
            float values[] = {(in[0] + in[1]) / 2, (out[0] + out[1]) / 2, maxDrive / 20};
            meters.process(values);

            // next sample
            ++offset;
        } // cycle trough samples
        
        // clean up
        lp[0][0].sanitize();
        lp[1][0].sanitize();
        lp[0][1].sanitize();
        lp[1][1].sanitize();
        lp[0][2].sanitize();
        lp[1][2].sanitize();
        lp[0][3].sanitize();
        lp[1][3].sanitize();
        hp[0][0].sanitize();
        hp[1][0].sanitize();
        hp[0][1].sanitize();
        hp[1][1].sanitize();
        hp[0][2].sanitize();
        hp[1][2].sanitize();
        hp[0][3].sanitize();
        hp[1][3].sanitize();
        p[0].sanitize();
        p[1].sanitize();
    }
    meters.fall(numsamples);
    return outputs_mask;
}

/**********************************************************************
 * EXCITER by Markus Schmidt
**********************************************************************/

exciter_audio_module::exciter_audio_module()
{
    freq_old        = 0.f;
    ceil_old        = 0.f;
    ceil_active_old = false;    
    meter_drive     = 0.f;
    is_active       = false;
    srate           = 0;
}

void exciter_audio_module::activate()
{
    is_active = true;
    // set all filters
    params_changed();
}

void exciter_audio_module::deactivate()
{
    is_active = false;
}

void exciter_audio_module::params_changed()
{
    // set the params of all filters
    if(*params[param_freq] != freq_old) {
        hp[0][0].set_hp_rbj(*params[param_freq], 0.707, (float)srate);
        hp[0][1].copy_coeffs(hp[0][0]);
        hp[0][2].copy_coeffs(hp[0][0]);
        hp[0][3].copy_coeffs(hp[0][0]);
        if(in_count > 1 && out_count > 1) {
            hp[1][0].copy_coeffs(hp[0][0]);
            hp[1][1].copy_coeffs(hp[0][0]);
            hp[1][2].copy_coeffs(hp[0][0]);
            hp[1][3].copy_coeffs(hp[0][0]);
        }
        freq_old = *params[param_freq];
    }
    // set the params of all filters
    if(*params[param_ceil] != ceil_old or *params[param_ceil_active] != ceil_active_old) {
        lp[0][0].set_lp_rbj(*params[param_ceil], 0.707, (float)srate);
        lp[0][1].copy_coeffs(lp[0][0]);
        lp[1][0].copy_coeffs(lp[0][0]);
        lp[1][1].copy_coeffs(lp[0][0]);
        ceil_old = *params[param_ceil];
        ceil_active_old = *params[param_ceil_active];
    }
    // set distortion
    dist[0].set_params(*params[param_blend], *params[param_drive]);
    if(in_count > 1 && out_count > 1)
        dist[1].set_params(*params[param_blend], *params[param_drive]);
}

void exciter_audio_module::set_sample_rate(uint32_t sr)
{
    srate = sr;
    dist[0].set_sample_rate(sr);
    if(in_count > 1 && out_count > 1)
        dist[1].set_sample_rate(sr);
    int meter[] = {param_meter_in,  param_meter_out, param_meter_drive};
    int clip[] = {param_clip_in, param_clip_out, -1};
    meters.init(params, meter, clip, 3, srate);
}

uint32_t exciter_audio_module::process(uint32_t offset, uint32_t numsamples, uint32_t inputs_mask, uint32_t outputs_mask)
{
    bool bypass = *params[param_bypass] > 0.5f;
    numsamples += offset;
    if(bypass) {
        // everything bypassed
        while(offset < numsamples) {
            if(in_count > 1 && out_count > 1) {
                outs[0][offset] = ins[0][offset];
                outs[1][offset] = ins[1][offset];
            } else if(in_count > 1) {
                outs[0][offset] = (ins[0][offset] + ins[1][offset]) / 2;
            } else if(out_count > 1) {
                outs[0][offset] = ins[0][offset];
                outs[1][offset] = ins[0][offset];
            } else {
                outs[0][offset] = ins[0][offset];
            }
            float values[] = {0, 0, 0, 0, 0};
            meters.process(values);
            ++offset;
        }
        // displays, too
        meter_drive = 0.f;
    } else {
        
        meter_drive = 0.f;
        
        float in2out = *params[param_listen] > 0.f ? 0.f : 1.f;
        
        // process
        while(offset < numsamples) {
            // cycle through samples
            float out[2], in[2] = {0.f, 0.f};
            float maxDrive = 0.f;
            int c = 0;
            
            if(in_count > 1 && out_count > 1) {
                // stereo in/stereo out
                // handle full stereo
                in[0] = ins[0][offset];
                in[0] *= *params[param_level_in];
                in[1] = ins[1][offset];
                in[1] *= *params[param_level_in];
                c = 2;
            } else {
                // in and/or out mono
                // handle mono
                in[0] = ins[0][offset];
                in[0] *= *params[param_level_in];
                in[1] = in[0];
                c = 1;
            }
            
            float proc[2];
            proc[0] = in[0];
            proc[1] = in[1];
            
            for (int i = 0; i < c; ++i) {
                // all pre filters in chain
                proc[i] = hp[i][1].process(hp[i][0].process(proc[i]));
                
                // saturate
                proc[i] = dist[i].process(proc[i]);

                // all post filters in chain
                proc[i] = hp[i][2].process(hp[i][3].process(proc[i]));
                
                if(*params[param_ceil_active] > 0.5f) {
                    // all H/P post filters in chain
                    proc[i] = lp[i][0].process(lp[i][1].process(proc[i]));
                    
                }
            }
            maxDrive = dist[0].get_distortion_level() * *params[param_amount];
            
            if(in_count > 1 && out_count > 1) {
                maxDrive = std::max(maxDrive, dist[1].get_distortion_level() * *params[param_amount]);
                // full stereo
                out[0] = (proc[0] * *params[param_amount] + in2out * in[0]) * *params[param_level_out];
                out[1] = (proc[1] * *params[param_amount] + in2out * in[1]) * *params[param_level_out];
                outs[0][offset] = out[0];
                outs[1][offset] = out[1];
            } else if(out_count > 1) {
                // mono -> pseudo stereo
                out[1] = out[0] = (proc[0] * *params[param_amount] + in2out * in[0]) * *params[param_level_out];
                outs[0][offset] = out[0];
                outs[1][offset] = out[1];
            } else {
                // stereo -> mono
                // or full mono
                out[0] = (proc[0] * *params[param_amount] + in2out * in[0]) * *params[param_level_out];
                outs[0][offset] = out[0];
            }
            float values[] = {(in[0] + in[1]) / 2, (out[0] + out[1]) / 2, maxDrive};
            meters.process(values);
            // next sample
            ++offset;
        } // cycle trough samples
        // clean up
        hp[0][0].sanitize();
        hp[1][0].sanitize();
        hp[0][1].sanitize();
        hp[1][1].sanitize();
        hp[0][2].sanitize();
        hp[1][2].sanitize();
        hp[0][3].sanitize();
        hp[1][3].sanitize();
    }
    meters.fall(numsamples);
    return outputs_mask;
}

/**********************************************************************
 * BASS ENHANCER by Markus Schmidt
**********************************************************************/

bassenhancer_audio_module::bassenhancer_audio_module()
{
    freq_old         = 0.f;
    floor_old        = 0.f;
    floor_active_old = false;    
    meter_drive      = 0.f;
    is_active        = false;
    srate            = 0;
}

void bassenhancer_audio_module::activate()
{
    is_active = true;
    // set all filters
    params_changed();
}
void bassenhancer_audio_module::deactivate()
{
    is_active = false;
}

void bassenhancer_audio_module::params_changed()
{
    // set the params of all filters
    if(*params[param_freq] != freq_old) {
        lp[0][0].set_lp_rbj(*params[param_freq], 0.707, (float)srate);
        lp[0][1].copy_coeffs(lp[0][0]);
        lp[0][2].copy_coeffs(lp[0][0]);
        lp[0][3].copy_coeffs(lp[0][0]);
        if(in_count > 1 && out_count > 1) {
            lp[1][0].copy_coeffs(lp[0][0]);
            lp[1][1].copy_coeffs(lp[0][0]);
            lp[1][2].copy_coeffs(lp[0][0]);
            lp[1][3].copy_coeffs(lp[0][0]);
        }
        freq_old = *params[param_freq];
    }
    // set the params of all filters
    if(*params[param_floor] != floor_old or *params[param_floor_active] != floor_active_old) {
        hp[0][0].set_hp_rbj(*params[param_floor], 0.707, (float)srate);
        hp[0][1].copy_coeffs(hp[0][0]);
        hp[1][0].copy_coeffs(hp[0][0]);
        hp[1][1].copy_coeffs(hp[0][0]);
        floor_old = *params[param_floor];
        floor_active_old = *params[param_floor_active];
    }
    // set distortion
    dist[0].set_params(*params[param_blend], *params[param_drive]);
    if(in_count > 1 && out_count > 1)
        dist[1].set_params(*params[param_blend], *params[param_drive]);
}

void bassenhancer_audio_module::set_sample_rate(uint32_t sr)
{
    srate = sr;
    dist[0].set_sample_rate(sr);
    if(in_count > 1 && out_count > 1)
        dist[1].set_sample_rate(sr);
    int meter[] = {param_meter_in,  param_meter_out, param_meter_drive};
    int clip[] = {param_clip_in, param_clip_out, -1};
    meters.init(params, meter, clip, 3, srate);
}

uint32_t bassenhancer_audio_module::process(uint32_t offset, uint32_t numsamples, uint32_t inputs_mask, uint32_t outputs_mask)
{
    bool bypass = *params[param_bypass] > 0.5f;
    numsamples += offset;
    if(bypass) {
        // everything bypassed
        while(offset < numsamples) {
            if(in_count > 1 && out_count > 1) {
                outs[0][offset] = ins[0][offset];
                outs[1][offset] = ins[1][offset];
            } else if(in_count > 1) {
                outs[0][offset] = (ins[0][offset] + ins[1][offset]) / 2;
            } else if(out_count > 1) {
                outs[0][offset] = ins[0][offset];
                outs[1][offset] = ins[0][offset];
            } else {
                outs[0][offset] = ins[0][offset];
            }
            float values[] = {0, 0, 0};
            meters.process(values);
            ++offset;
        }
    } else {
        // process
        while(offset < numsamples) {
            // cycle through samples
            float out[2], in[2] = {0.f, 0.f};
            float maxDrive = 0.f;
            int c = 0;
            
            if(in_count > 1 && out_count > 1) {
                // stereo in/stereo out
                // handle full stereo
                in[0] = ins[0][offset];
                in[0] *= *params[param_level_in];
                in[1] = ins[1][offset];
                in[1] *= *params[param_level_in];
                c = 2;
            } else {
                // in and/or out mono
                // handle mono
                in[0] = ins[0][offset];
                in[0] *= *params[param_level_in];
                in[1] = in[0];
                c = 1;
            }
            
            float proc[2];
            proc[0] = in[0];
            proc[1] = in[1];
            
            for (int i = 0; i < c; ++i) {
                // all pre filters in chain
                proc[i] = lp[i][1].process(lp[i][0].process(proc[i]));
                
                // saturate
                proc[i] = dist[i].process(proc[i]);

                // all post filters in chain
                proc[i] = lp[i][2].process(lp[i][3].process(proc[i]));
                
                if(*params[param_floor_active] > 0.5f) {
                    // all H/P post filters in chain
                    proc[i] = hp[i][0].process(hp[i][1].process(proc[i]));
                    
                }
            }
            
            if(in_count > 1 && out_count > 1) {
                // full stereo
                if(*params[param_listen] > 0.f)
                    out[0] = proc[0] * *params[param_amount] * *params[param_level_out];
                else
                    out[0] = (proc[0] * *params[param_amount] + in[0]) * *params[param_level_out];
                outs[0][offset] = out[0];
                if(*params[param_listen] > 0.f)
                    out[1] = proc[1] * *params[param_amount] * *params[param_level_out];
                else
                    out[1] = (proc[1] * *params[param_amount] + in[1]) * *params[param_level_out];
                outs[1][offset] = out[1];
                maxDrive = std::max(dist[0].get_distortion_level() * *params[param_amount],
                                            dist[1].get_distortion_level() * *params[param_amount]);
            } else if(out_count > 1) {
                // mono -> pseudo stereo
                if(*params[param_listen] > 0.f)
                    out[0] = proc[0] * *params[param_amount] * *params[param_level_out];
                else
                    out[0] = (proc[0] * *params[param_amount] + in[0]) * *params[param_level_out];
                outs[0][offset] = out[0];
                out[1] = out[0];
                outs[1][offset] = out[1];
                maxDrive = dist[0].get_distortion_level() * *params[param_amount];
            } else {
                // stereo -> mono
                // or full mono
                if(*params[param_listen] > 0.f)
                    out[0] = proc[0] * *params[param_amount] * *params[param_level_out];
                else
                    out[0] = (proc[0] * *params[param_amount] + in[0]) * *params[param_level_out];
                outs[0][offset] = out[0];
                maxDrive = dist[0].get_distortion_level() * *params[param_amount];
            }
            
            float values[] = {(in[0] + in[1]) / 2, (out[0] + out[1]) / 2, maxDrive};
            meters.process(values);
            // next sample
            ++offset;
        } // cycle trough samples
        // clean up
        lp[0][0].sanitize();
        lp[1][0].sanitize();
        lp[0][1].sanitize();
        lp[1][1].sanitize();
        lp[0][2].sanitize();
        lp[1][2].sanitize();
        lp[0][3].sanitize();
        lp[1][3].sanitize();
    }
    meters.fall(numsamples);
    return outputs_mask;
}

/**********************************************************************
 * TAPESIMULATOR by Markus Schmidt
**********************************************************************/

tapesimulator_audio_module::tapesimulator_audio_module() {
    active          = false;
    clip_inL        = 0.f;
    clip_inR        = 0.f;
    clip_outL       = 0.f;
    clip_outR       = 0.f;
    meter_inL       = 0.f;
    meter_inR       = 0.f;
    meter_outL      = 0.f;
    meter_outR      = 0.f;
    lp_old          = -1.f;
    rms             = 0.f;
    mech_old        = false;
    transients.set_channels(channels);
}

void tapesimulator_audio_module::activate() {
    active = true;
}

void tapesimulator_audio_module::deactivate() {
    active = false;
}

void tapesimulator_audio_module::params_changed() {
    if(*params[param_lp] != lp_old or *params[param_mechanical] != mech_old) {
        lp[0][0].set_lp_rbj(*params[param_lp], 0.707, (float)srate);
        lp[0][1].copy_coeffs(lp[0][0]);
        lp[1][0].copy_coeffs(lp[0][0]);
        lp[1][1].copy_coeffs(lp[0][0]);
        lp_old = *params[param_lp];
        mech_old = *params[param_mechanical] > 0.5;
    }
    transients.set_params(50.f / (*params[param_speed] + 1),
                          -0.05f / (*params[param_speed] + 1),
                          100.f,
                          0.f,
                          1.f,
                          0,
                          1);
    lfo1.set_params((*params[param_speed] + 1) / 2, 0, 0.f, srate, 1.f);
    lfo2.set_params((*params[param_speed] + 1) / 9.38, 0, 0.f, srate, 1.f);
}

uint32_t tapesimulator_audio_module::process(uint32_t offset, uint32_t numsamples, uint32_t inputs_mask, uint32_t outputs_mask) {
    for(uint32_t i = offset; i < offset + numsamples; i++) {
        float L = ins[0][i];
        float R = ins[1][i];
        float Lin = ins[0][i];
        float Rin = ins[1][i];
        if(*params[param_bypass] > 0.5) {
            outs[0][i]  = ins[0][i];
            outs[1][i]  = ins[1][i];
            clip_inL    = 0.f;
            clip_inR    = 0.f;
            clip_outL   = 0.f;
            clip_outR   = 0.f;
            meter_inL   = 0.f;
            meter_inR   = 0.f;
            meter_outL  = 0.f;
            meter_outR  = 0.f;
        } else {
            // let meters fall a bit
            clip_inL    -= std::min(clip_inL,  numsamples);
            clip_inR    -= std::min(clip_inR,  numsamples);
            clip_outL   -= std::min(clip_outL, numsamples);
            clip_outR   -= std::min(clip_outR, numsamples);
            meter_inL    = 0.f;
            meter_inR    = 0.f;
            meter_outL   = 0.f;
            meter_outR   = 0.f;
            
            // GUI stuff
            if(L > meter_inL) meter_inL = L;
            if(R > meter_inR) meter_inR = R;
            if(L > 1.f) clip_inL  = srate >> 3;
            if(R > 1.f) clip_inR  = srate >> 3;
            
            // transients
            if(*params[param_magnetical] > 0.5f) {
                float values[] = {L, R};
                transients.process(values);
                L = values[0];
                R = values[1];
            }
            
            // noise
            if (*params[param_noise]) {
                float Lnoise = rand() % 2 - 1;
                float Rnoise = rand() % 2 - 1;
                Lnoise = noisefilters[0][2].process(noisefilters[0][1].process(noisefilters[0][0].process(Lnoise)));
                Rnoise = noisefilters[1][2].process(noisefilters[1][1].process(noisefilters[1][0].process(Rnoise)));
                L += Lnoise * *params[param_noise] / 12.f;
                R += Rnoise * *params[param_noise] / 12.f;
            }
            
            // lfo filters / phasing
            if (*params[param_mechanical]) {
                // filtering
                float freqL1 = *params[param_lp] * (1 - ((lfo1.get_value() + 1) * 0.3 * *params[param_mechanical]));
                float freqL2 = *params[param_lp] * (1 - ((lfo2.get_value() + 1) * 0.2 * *params[param_mechanical]));
                
                float freqR1 = *params[param_lp] * (1 - ((lfo1.get_value() * -1 + 1) * 0.3 * *params[param_mechanical]));
                float freqR2 = *params[param_lp] * (1 - ((lfo2.get_value() * -1 + 1) * 0.2 * *params[param_mechanical]));
                
                lp[0][0].set_lp_rbj(freqL1, 0.707, (float)srate);
                lp[0][1].set_lp_rbj(freqL2, 0.707, (float)srate);
                    
                lp[1][0].set_lp_rbj(freqR1, 0.707, (float)srate);
                lp[1][1].set_lp_rbj(freqR2, 0.707, (float)srate);
                
                // phasing
                float _phase = lfo1.get_value() * *params[param_mechanical] * -36;
                
                float _phase_cos_coef = cos(_phase / 180 * M_PI);
                float _phase_sin_coef = sin(_phase / 180 * M_PI);
                
                float _l = L * _phase_cos_coef - R * _phase_sin_coef;
                float _r = L * _phase_sin_coef + R * _phase_cos_coef;
                L = _l;
                R = _r;
            }
            
            // gain
            L *= *params[param_level_in];
            R *= *params[param_level_in];
            
            // save for drawing input/output curve
            float Lc = L;
            float Rc = R;
            float Lo = L;
            float Ro = R;
            
            // filter
            if (*params[param_post] < 0.5) {
                L = lp[0][1].process(lp[0][0].process(L));
                R = lp[1][1].process(lp[1][0].process(R));
            }
            
            // distortion
            if (L) L = L / fabs(L) * (1 - exp((-1) * 3 * fabs(L)));
            if (R) R = R / fabs(R) * (1 - exp((-1) * 3 * fabs(R)));
            
            if (Lo) Lo = Lo / fabs(Lo) * (1 - exp((-1) * 3 * fabs(Lo)));
            if (Ro) Ro = Ro / fabs(Ro) * (1 - exp((-1) * 3 * fabs(Ro)));
            
            // filter
            if (*params[param_post] >= 0.5) {
                L = lp[0][1].process(lp[0][0].process(L));
                R = lp[1][1].process(lp[1][0].process(R));
            }
            
            // mix
            L = L * *params[param_mix] + Lin * (*params[param_mix] * -1 + 1);
            R = R * *params[param_mix] + Rin * (*params[param_mix] * -1 + 1);
            
            // levels out
            L *= *params[param_level_out];
            R *= *params[param_level_out];
            
            Lo *= *params[param_level_out];
            Ro *= *params[param_level_out];
            
            // output
            outs[0][i] = L;
            outs[1][i] = R;
            
            // clip LED's
            if(L > 1.f) clip_outL = srate >> 3;
            if(R > 1.f) clip_outR = srate >> 3;
            if(L > meter_outL) meter_outL = L;
            if(R > meter_outR) meter_outR = R;
            
            // sanitize filters
            lp[0][0].sanitize();
            lp[1][0].sanitize();
            lp[0][1].sanitize();
            lp[1][1].sanitize();
            
            // LFO's should go on
            lfo1.advance(1);
            lfo2.advance(1);
        
            float s = (fabs(Lo) + fabs(Ro)) / 2;
            if (s > rms) {
                rms = s;
            }
            float in = (fabs(Lc) + fabs(Rc)) / 2;
            if (in > input) {
                input = in;
            }
        }
    }
    // draw meters
    SET_IF_CONNECTED(clip_inL);
    SET_IF_CONNECTED(clip_inR);
    SET_IF_CONNECTED(clip_outL);
    SET_IF_CONNECTED(clip_outR);
    SET_IF_CONNECTED(meter_inL);
    SET_IF_CONNECTED(meter_inR);
    SET_IF_CONNECTED(meter_outL);
    SET_IF_CONNECTED(meter_outR);
    return outputs_mask;
}

void tapesimulator_audio_module::set_sample_rate(uint32_t sr)
{
    srate = sr;
    transients.set_sample_rate(srate);
    noisefilters[0][0].set_hp_rbj(120.f, 0.707, (float)srate);
    noisefilters[1][0].copy_coeffs(noisefilters[0][0]);
    noisefilters[0][1].set_lp_rbj(5500.f, 0.707, (float)srate);
    noisefilters[1][1].copy_coeffs(noisefilters[0][1]);
    noisefilters[0][2].set_highshelf_rbj(1000.f, 0.707, 0.5, (float)srate);
    noisefilters[1][2].copy_coeffs(noisefilters[0][2]);
}

bool tapesimulator_audio_module::get_graph(int index, int subindex, int phase, float *data, int points, cairo_iface *context, int *mode) const
{
    if (subindex > 1)
        return false;
    if(index == param_lp and phase) {
        set_channel_color(context, subindex);
        return ::get_graph(*this, subindex, data, points);
    } else if (index == param_level_in and !phase) {
        if (!subindex) {
            context->set_source_rgba(0.15, 0.2, 0.0, 0.3);
            context->set_line_width(1);
        }
        for (int i = 0; i < points; i++) {
            if (!subindex) {
                float input = dB_grid_inv(-1.0 + (float)i * 2.0 / ((float)points - 1.f));
                data[i] = dB_grid(input);
            } else {
                float output = 1 - exp(-3 * (pow(2, -10 + 14 * (float)i / (float) points)));
                data[i] = dB_grid(output * *params[param_level_out]);
            }
        }
        return true;
    }
    return false;
}
float tapesimulator_audio_module::freq_gain(int index, double freq) const
{
    return lp[index][0].freq_gain(freq, srate) * lp[index][1].freq_gain(freq, srate);
}

bool tapesimulator_audio_module::get_gridline(int index, int subindex, int phase, float &pos, bool &vertical, std::string &legend, cairo_iface *context) const
{
    if(!active or phase)
        return false;
    if (index == param_level_in) {
        bool tmp;
        vertical = (subindex & 1) != 0;
        bool result = get_freq_gridline(subindex >> 1, pos, tmp, legend, context, false);
        if (result && vertical) {
            if ((subindex & 4) && !legend.empty()) {
                legend = "";
            }
            else {
                size_t pos = legend.find(" dB");
                if (pos != std::string::npos)
                    legend.erase(pos);
            }
            pos = 0.5 + 0.5 * pos;
        }
        return result;
    } else if (index == param_lp) {
        return get_freq_gridline(subindex, pos, vertical, legend, context);
    }
    return false;
}
bool tapesimulator_audio_module::get_dot(int index, int subindex, int phase, float &x, float &y, int &size, cairo_iface *context) const
{
    if (index == param_level_in and !subindex and phase) {
        x = log(input) / log(2) / 14.f + 5.f / 7.f;
        y = dB_grid(rms);
        rms = 0.f;
        input = 0.f;
        return true;
    }
    return false;
}
bool tapesimulator_audio_module::get_layers(int index, int generation, unsigned int &layers) const
{
    layers = 0;
    // always draw grid on cache if surfaces are new on both widgets
    if (!generation)
        layers |= LG_CACHE_GRID;
    // compression: dot in realtime, graphs as cache on new surfaces
    if (index == param_level_in and !generation)
        layers |= LG_CACHE_GRAPH;
    if (index == param_level_in)
        layers |= LG_REALTIME_DOT;
    // frequency: both graphs in realtime
    if (index == param_lp)
        layers |= LG_REALTIME_GRAPH;
    // draw always
    return true;
}



/**********************************************************************
 * CRUSHER by Markus Schmidt
**********************************************************************/


crusher_audio_module::crusher_audio_module()
{
    
}

void crusher_audio_module::activate()
{
    
}
void crusher_audio_module::deactivate()
{
    
}

void crusher_audio_module::params_changed()
{
    bitreduction.set_params(*params[param_bits], *params[param_morph], *params[param_bypass] > 0.5, *params[param_mode], *params[param_round] > 0.5, *params[param_offset], *params[param_dc], *params[param_aa]);
}

void crusher_audio_module::set_sample_rate(uint32_t sr)
{
    srate = sr;
    int meter[] = {param_meter_inL,  param_meter_inR, param_meter_outL, param_meter_outR};
    int clip[]  = {param_clip_inL, param_clip_inR, param_clip_outL, param_clip_outR};
    meters.init(params, meter, clip, 4, srate);
    bitreduction.set_sample_rate(srate);
}

uint32_t crusher_audio_module::process(uint32_t offset, uint32_t numsamples, uint32_t inputs_mask, uint32_t outputs_mask)
{
    bool bypass = *params[param_bypass] > 0.5f;
    numsamples += offset;
    if(bypass) {
        // everything bypassed
        while(offset < numsamples) {
            outs[0][offset] = ins[0][offset];
            outs[1][offset] = ins[1][offset];
            float values[] = {0, 0, 0, 0};
            meters.process(values);
            ++offset;
        }
    } else {
        // process
        while(offset < numsamples) {
            // cycle through samples
            outs[0][offset] = bitreduction.process(ins[0][offset] * *params[param_level_in]) * *params[param_level_out];
            outs[1][offset] = bitreduction.process(ins[1][offset] * *params[param_level_in]) * *params[param_level_out];
            
            float values[] = {ins[0][offset], ins[1][offset], outs[0][offset], outs[1][offset]};
            meters.process(values);
            // next sample
            ++offset;
        } // cycle trough samples
    }
    meters.fall(numsamples);
    return outputs_mask;
}
bool crusher_audio_module::get_graph(int index, int subindex, int phase, float *data, int points, cairo_iface *context, int *mode) const
{
    return bitreduction.get_graph(subindex, phase, data, points, context, mode);
}
bool crusher_audio_module::get_layers(int index, int generation, unsigned int &layers) const
{
    return bitreduction.get_layers(index, generation, layers);
}
bool crusher_audio_module::get_gridline(int index, int subindex, int phase, float &pos, bool &vertical, std::string &legend, cairo_iface *context) const
{
    return bitreduction.get_gridline(subindex, phase, pos, vertical, legend, context);
}

%------------------------------------------------------------------------
% NAME:    hAntennaGaussAdv
%
%          Includes a Gaussian antenna pattern into H with options to
%          scale the pattern with frequency and to consider a moving antenna.
%
%          See H_ANTENNA for definition of frequency scaling and movement
%          variables.
%
% FORMAT:  [H,f_y,za_y,za_sensor] = hAntennaGaussAdv(H,f_sensor,za_sensor,
%                    za_obs,fwhm,width,spacing,o_ant,o_y,fscale,f0,move,dza)
%
% RETURN:  H           H matrix after antenna
%          f_y         new frequency vector
%          za_y        new zenith angle vector 
%          za_sensor   new zenith angles, set to ZA_OBS
% IN:      H           H matrix before the antenna
%          f_sensor    frequencies
%          za_sensor   input zenith angles
%          za_obs      zenith angles observed by the sensor
%          fwhm        full width at half mean of the antenna pattern [degs]
%          width       total width of the antenna pattern to consider [degs]
%          spacing     maximum spacing of the abscissa to use for the 
%                      antenna pattern [deg]
%          o_ant       linear (=1) or cubic (=3) treatment of the antenna 
%                      pattern
%          o_y         linear (=1) or cubic (=3) treatment of spectra
%          fscale      flag to scale the pattern with frequency
%          f0          reference frequency for frequency scaling, i.e. for 
%                      which frequency FWHM is valid
%          move        flag to consider a moving antenna with a constant
%                      scanning velocity during the integration
%          dza         total movement during the integration [deg]
%------------------------------------------------------------------------

% HISTORY: 00.08.25  Created by Patrick Eriksson. 
%          00.11.16  Included linear/cubic flags (PE) 
%          02.01.04  Moved calculation of response to dedicated function (PE)


function [H,f_y,za_y,za_sensor] = hAntennaGaussAdv(...
 H,f_sensor,za_sensor,za_obs,fwhm,width,spacing,o_ant,o_y,fscale,f0,move,dza)


%=== Create a gaussian response 
%
% width and spacing must be converted to "standard deviations"
%
si      = fwhm/sqrt(2*log(2))/2;
%
width   = width / si;
spacing = si / spacing;
% 
[za_ant,ant] = gaussian_response( fwhm, width, spacing );


%=== Get H for the antenna pattern
[Hant,za_sensor] = ...
         h_antenna(f_sensor,za_sensor,za_obs,za_ant,ant,o_ant,o_y,...
                                                         fscale,f0,move,dza);


%=== Include Hant in H
H = h_x_h(Hant,H);


%=== Create new F_Y and ZA_Y
[f_y,za_y] = h_fix_ys(f_sensor,za_sensor);

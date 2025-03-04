/* Copyright (C) 2000-2013
   Stefan Buehler  <sbuehler@ltu.se>
   Axel von Engeln <engeln@uni-bremen.de>

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
   USA. */

/** \file

  LineRecord implementation.

  \author Stefan Buehler and Axel von Engeln
*/

#include "linerecord.h"
#include <cfloat>
#include "absorption.h"
#include "quantum_parser_hitran.h"

#include "global_data.h"

#include "file.h"

String LineRecord::VersionString() const {
  ostringstream os;
  os << "ARTSCAT-" << mversion;
  return os.str();
}

String LineRecord::Name() const {
  // The species lookup data:
  using global_data::species_data;
  const SpeciesRecord& sr = species_data[mqid.Species()];
  return sr.Name() + "-" + sr.Isotopologue()[mqid.Isotopologue()].Name();
}

const SpeciesRecord& LineRecord::SpeciesData() const {
  // The species lookup data:
  using global_data::species_data;
  return species_data[mqid.Species()];
}

const IsotopologueRecord& LineRecord::IsotopologueData() const {
  // The species lookup data:
  using global_data::species_data;
  return species_data[mqid.Species()].Isotopologue()[mqid.Isotopologue()];
}

bool LineRecord::ReadFromHitran2001Stream(istream& is,
                                          const Verbosity& verbosity) {
  CREATE_OUT3;

  // Global species lookup data:
  using global_data::species_data;

  // This value is used to flag missing data both in species and
  // isotopologue lists. Could be any number, it just has to be made sure
  // that it is neither the index of a species nor of an isotopologue.
  const Index missing = species_data.nelem() + 100;

  // We need a species index sorted by HITRAN tag. Keep this in a
  // static variable, so that we have to do this only once.  The ARTS
  // species index is hind[<HITRAN tag>].
  //
  // Allow for up to 100 species in HITRAN in the future.
  static Array<Index> hspec(100);

  // This is  an array of arrays for each hitran tag. It contains the
  // ARTS indices of the HITRAN isotopologues.
  static Array<ArrayOfIndex> hiso(100);

  // Remember if this stuff has already been initialized:
  static bool hinit = false;

  // Remember, about which missing species we have already issued a
  // warning:
  static ArrayOfIndex warned_missing;

  if (!hinit) {
    // Initialize hspec.
    // The value of missing means that we don't have this species.
    hspec = missing;  // Matpack can set all elements like this.

    for (Index i = 0; i < species_data.nelem(); ++i) {
      const SpeciesRecord& sr = species_data[i];

      // We have to be careful and check for the case that all
      // HITRAN isotopologue tags are -1 (this species is missing in HITRAN).

      if (sr.Isotopologue().nelem() && 0 < sr.Isotopologue()[0].HitranTag()) {
        // The HITRAN tags are stored as species plus isotopologue tags
        // (MO and ISO)
        // in the Isotopologue() part of the species record.
        // We can extract the MO part from any of the isotopologue tags,
        // so we use the first one. We do this by taking an integer
        // division by 10.

        Index mo = sr.Isotopologue()[0].HitranTag() / 10;
        //          cout << "mo = " << mo << endl;
        hspec[mo] = i;

        // Get a nicer to handle array of HITRAN iso tags:
        Index n_iso = sr.Isotopologue().nelem();
        ArrayOfIndex iso_tags;
        iso_tags.resize(n_iso);
        for (Index j = 0; j < n_iso; ++j) {
          iso_tags[j] = sr.Isotopologue()[j].HitranTag();
        }

        // Reserve elements for the isotopologue tags. How much do we
        // need? This depends on the largest HITRAN tag that we know
        // about!
        // Also initialize the tags to missing.
        //          cout << "iso_tags = " << iso_tags << endl;
        //          cout << "static_cast<Index>(max(iso_tags))%10 + 1 = "
        //               << static_cast<Index>(max(iso_tags))%10 + 1 << endl;
        hiso[mo].resize(max(iso_tags) % 10 + 1);
        hiso[mo] = missing;  // Matpack can set all elements like this.

        // Set the isotopologue tags:
        for (Index j = 0; j < n_iso; ++j) {
          if (0 < iso_tags[j])  // ignore -1 elements
          {
            // To get the iso tags from HitranTag() we also have to take
            // modulo 10 to get rid of mo.
            hiso[mo][iso_tags[j] % 10] = j;
          }
        }
      }
    }

    // Print the generated data structures (for debugging):
    out3 << "  HITRAN index table:\n";
    for (Index i = 0; i < hspec.nelem(); ++i) {
      if (missing != hspec[i]) {
        // The explicit conversion of Name to a c-String is
        // necessary, because setw does not work correctly for
        // stl Strings.
        out3 << "  mo = " << i << "   Species = " << setw(10)
             << setiosflags(ios::left) << species_data[hspec[i]].Name().c_str()
             << "iso = ";
        for (Index j = 1; j < hiso[i].nelem(); ++j) {
          if (missing == hiso[i][j])
            out3 << " "
                 << "m";
          else
            out3 << " "
                 << species_data[hspec[i]].Isotopologue()[hiso[i][j]].Name();
        }
        out3 << "\n";
      }
    }

    hinit = true;
  }

  // This contains the rest of the line to parse. At the beginning the
  // entire line. Line gets shorter and shorter as we continue to
  // extract stuff from the beginning.
  String line;

  // The first item is the molecule number:
  Index mo;

  // Look for more comments?
  bool comment = true;

  while (comment) {
    // Return true if eof is reached:
    if (is.eof()) return true;

    // Throw runtime_error if stream is bad:
    if (!is) throw runtime_error("Stream bad.");

    // Read line from file into linebuffer:
    getline(is, line);

    // It is possible that we were exactly at the end of the file before
    // calling getline. In that case the previous eof() was still false
    // because eof() evaluates only to true if one tries to read after the
    // end of the file. The following check catches this.
    if (line.nelem() == 0 && is.eof()) return true;

    // If the catalogue is in dos encoding, throw away the
    // additional carriage return
    if (line[line.nelem() - 1] == 13) {
      line.erase(line.nelem() - 1, 1);
    }

    // Because of the fixed FORTRAN format, we need to break up the line
    // explicitly in apropriate pieces. Not elegant, but works!

    // Extract molecule number:
    mo = 0;
    // Initialization of mo is important, because mo stays the same
    // if line is empty.
    extract(mo, line, 2);
    //      cout << "mo = " << mo << endl;

    // If mo == 0 this is just a comment line:
    if (0 != mo) {
      // See if we know this species. Exit with an error if the species is unknown.
      if (missing != hspec[mo]) {
        comment = false;

        // Check if data record has the right number of characters for the
        // in Hitran 1986-2001 format
        Index nChar = line.nelem() + 2;  // number of characters in data record;
        if (nChar != 100) {
          ostringstream os;
          os << "Invalid HITRAN 1986-2001 line data record with " << nChar
             << " characters (expected: 100)." << endl
             << line << " n: " << line.nelem();
          throw runtime_error(os.str());
        }

      } else {
        // See if this is already in warned_missing, use
        // std::count for that:
        if (0 == std::count(warned_missing.begin(), warned_missing.end(), mo)) {
          CREATE_OUT0;
          out0 << "Error: HITRAN mo = " << mo << " is not "
               << "known to ARTS.\n";
          warned_missing.push_back(mo);
        }
      }
    }
  }

  // Ok, we seem to have a valid species here.

  // Set mspecies from my cool index table:
  mqid.SetSpecies(hspec[mo]);

  // Extract isotopologue:
  Index iso;
  extract(iso, line, 1);
  //  cout << "iso = " << iso << endl;

  // Set misotopologue from the other cool index table.
  // We have to be careful to issue an error for unknown iso tags. Iso
  // could be either larger than the size of hiso[mo], or set
  // explicitly to missing. Unfortunately we have to test both cases.
  mqid.SetIsotopologue(missing);
  if (iso < hiso[mo].nelem())
    if (missing != hiso[mo][iso]) mqid.SetIsotopologue(hiso[mo][iso]);

  // Issue error message if misotopologue is still missing:
  if (missing == mqid.Isotopologue()) {
    ostringstream os;
    os << "Species: " << species_data[mqid.Species()].Name()
       << ", isotopologue iso = " << iso << " is unknown.";
    throw runtime_error(os.str());
  }

  // Position.
  {
    // HITRAN position in wavenumbers (cm^-1):
    Numeric v;
    // External constant from constants.cc:
    extern const Numeric SPEED_OF_LIGHT;
    // Conversion from wavenumber to Hz. If you multiply a line
    // position in wavenumber (cm^-1) by this constant, you get the
    // frequency in Hz.
    const Numeric w2Hz = SPEED_OF_LIGHT * 100.;

    // Extract HITRAN postion:
    extract(v, line, 12);

    // ARTS position in Hz:
    mf = v * w2Hz;
    //    cout << "mf = " << mf << endl;
  }

  // Intensity.
  {
    extern const Numeric SPEED_OF_LIGHT;  // in [m/s]

    // HITRAN intensity is in cm-1/(molec * cm-2) at 296 Kelvin.
    // It already includes the isotpic ratio.
    // The first cm-1 is the frequency unit (it cancels with the
    // 1/frequency unit of the line shape function).
    //
    // We need to do the following:
    // 1. Convert frequency from wavenumber to Hz (factor 1e2 * c).
    // 2. Convert [molec * cm-2] to [molec * m-2] (factor 1e-4).
    // 3. Take out the isotopologue ratio.

    const Numeric hi2arts = 1e-2 * SPEED_OF_LIGHT;

    Numeric s;

    // Extract HITRAN intensity:
    extract(s, line, 10);
    // Convert to ARTS units (Hz / (molec * m-2) ), or shorter: Hz*m^2
    mi0 = s * hi2arts;
    // Take out isotopologue ratio:
    mi0 /= species_data[mqid.Species()]
               .Isotopologue()[mqid.Isotopologue()]
               .Abundance();
  }

  // Skip transition probability:
  {
    Numeric r;
    extract(r, line, 10);
  }

  // Air broadening parameters.
  Numeric agam, sgam;
  {
    // HITRAN parameter is in cm-1/atm at 296 Kelvin
    // All parameters are HWHM (I hope this is true!)
    Numeric gam;
    // External constant from constants.cc: Converts atm to
    // Pa. Multiply value in atm by this number to get value in Pa.
    extern const Numeric ATM2PA;
    // External constant from constants.cc:
    extern const Numeric SPEED_OF_LIGHT;
    // Conversion from wavenumber to Hz. If you multiply a value in
    // wavenumber (cm^-1) by this constant, you get the value in Hz.
    const Numeric w2Hz = SPEED_OF_LIGHT * 1e2;
    // Ok, put together the end-to-end conversion that we need:
    const Numeric hi2arts = w2Hz / ATM2PA;

    // Extract HITRAN AGAM value:
    extract(gam, line, 5);

    // ARTS parameter in Hz/Pa:
    agam = gam * hi2arts;

    // Extract HITRAN SGAM value:
    extract(gam, line, 5);

    // ARTS parameter in Hz/Pa:
    sgam = gam * hi2arts;

    // If zero, set to agam:
    if (0 == sgam) sgam = agam;

    //    cout << "agam, sgam = " << magam << ", " << msgam << endl;
  }

  // Lower state energy.
  {
    // HITRAN parameter is in wavenumbers (cm^-1).
    // We have to convert this to the ARTS unit Joule.

    // Extract from Catalogue line
    extract(melow, line, 10);

    // Convert to Joule:
    melow = wavenumber_to_joule(melow);
  }

  // Temperature coefficient of broadening parameters.
  Numeric nair, nself;
  {
    // This is dimensionless, we can also extract directly.
    extract(nair, line, 4);

    // Set self broadening temperature coefficient to the same value:
    nself = nair;
    //    cout << "mnair = " << mnair << endl;
  }

  // Pressure shift.
  Numeric psf;
  {
    // HITRAN value in cm^-1 / atm. So the conversion goes exactly as
    // for the broadening parameters.
    Numeric d;
    // External constant from constants.cc: Converts atm to
    // Pa. Multiply value in atm by this number to get value in Pa.
    extern const Numeric ATM2PA;
    // External constant from constants.cc:
    extern const Numeric SPEED_OF_LIGHT;
    // Conversion from wavenumber to Hz. If you multiply a value in
    // wavenumber (cm^-1) by this constant, you get the value in Hz.
    const Numeric w2Hz = SPEED_OF_LIGHT * 1e2;
    // Ok, put together the end-to-end conversion that we need:
    const Numeric hi2arts = w2Hz / ATM2PA;

    // Extract HITRAN value:
    extract(d, line, 8);

    // ARTS value in Hz/Pa
    psf = d * hi2arts;
  }
  // Set the accuracies using the definition of HITRAN
  // indices. If some are missing, they are set to -1.

  //Skip upper state global quanta index
  {
    Index eu;
    extract(eu, line, 3);
  }

  //Skip lower state global quanta index
  {
    Index el;
    extract(el, line, 3);
  }

  //Skip upper state local quanta
  {
    Index eul;
    extract(eul, line, 9);
  }

  //Skip lower state local quanta
  {
    Index ell;
    extract(ell, line, 9);
  }

  // Accuracy index for frequency reference
  {
    Index df;
    // Extract HITRAN value:
    extract(df, line, 1);
  }

  // Accuracy index for intensity reference
  {
    Index di0;
    // Extract HITRAN value:
    extract(di0, line, 1);
  }

  // Accuracy index for halfwidth reference
  {
    Index dgam;
    // Extract HITRAN value:
    extract(dgam, line, 1);
  }

  // These were all the parameters that we can extract from
  // HITRAN. However, we still have to set the reference temperatures
  // to the appropriate value:

  // Reference temperature for Intensity in K.
  mti0 = 296.0;

  // Set line shape computer
  mlineshapemodel = LineShape::Model(sgam, nself, agam, nair, psf);
  mstandard = true;

  // That's it!
  return false;
}

// The below is a derivative of ReadFromHitran2001Stream
bool LineRecord::ReadFromLBLRTMStream(istream& is, const Verbosity& verbosity) {
  CREATE_OUT3;

  // Global species lookup data:
  using global_data::species_data;

  // This value is used to flag missing data both in species and
  // isotopologue lists. Could be any number, it just has to be made sure
  // that it is neither the index of a species nor of an isotopologue.
  const Index missing = species_data.nelem() + 100;

  // We need a species index sorted by HITRAN tag. Keep this in a
  // static variable, so that we have to do this only once.  The ARTS
  // species index is hind[<HITRAN tag>].
  //
  // Allow for up to 100 species in HITRAN in the future.
  static Array<Index> hspec(100);

  // This is  an array of arrays for each hitran tag. It contains the
  // ARTS indices of the HITRAN isotopologues.
  static Array<ArrayOfIndex> hiso(100);

  // Remember if this stuff has already been initialized:
  static bool hinit = false;

  // Remember, about which missing species we have already issued a
  // warning:
  static ArrayOfIndex warned_missing;

  if (!hinit) {
    // Initialize hspec.
    // The value of missing means that we don't have this species.
    hspec = missing;  // Matpack can set all elements like this.
    for (Index i = 0; i < species_data.nelem(); ++i) {
      const SpeciesRecord& sr = species_data[i];
      // We have to be careful and check for the case that all
      // HITRAN isotopologue tags are -1 (this species is missing in HITRAN).
      if (sr.Isotopologue().nelem() && 0 < sr.Isotopologue()[0].HitranTag()) {
        // The HITRAN tags are stored as species plus isotopologue tags
        // (MO and ISO)
        // in the Isotopologue() part of the species record.
        // We can extract the MO part from any of the isotopologue tags,
        // so we use the first one. We do this by taking an integer
        // division by 10.

        Index mo = sr.Isotopologue()[0].HitranTag() / 10;
        //          cout << "mo = " << mo << endl;
        hspec[mo] = i;

        // Get a nicer to handle array of HITRAN iso tags:
        Index n_iso = sr.Isotopologue().nelem();
        ArrayOfIndex iso_tags;
        iso_tags.resize(n_iso);
        for (Index j = 0; j < n_iso; ++j) {
          iso_tags[j] = sr.Isotopologue()[j].HitranTag();
        }

        // Reserve elements for the isotopologue tags. How much do we
        // need? This depends on the largest HITRAN tag that we know
        // about!
        // Also initialize the tags to missing.
        //          cout << "iso_tags = " << iso_tags << endl;
        //          cout << "static_cast<Index>(max(iso_tags))%10 + 1 = "
        //               << static_cast<Index>(max(iso_tags))%10 + 1 << endl;
        hiso[mo].resize(max(iso_tags) % 10 + 1);
        hiso[mo] = missing;  // Matpack can set all elements like this.

        // Set the isotopologue tags:
        for (Index j = 0; j < n_iso; ++j) {
          if (0 < iso_tags[j])  // ignore -1 elements
          {
            // To get the iso tags from HitranTag() we also have to take
            // modulo 10 to get rid of mo.
            hiso[mo][iso_tags[j] % 10] = j;
          }
        }
      }
    }

    // Print the generated data structures (for debugging):
    out3 << "  HITRAN index table:\n";
    for (Index i = 0; i < hspec.nelem(); ++i) {
      if (missing != hspec[i]) {
        // The explicit conversion of Name to a c-String is
        // necessary, because setw does not work correctly for
        // stl Strings.
        out3 << "  mo = " << i << "   Species = " << std::setw(10)
             << std::setiosflags(std::ios::left)
             << species_data[hspec[i]].Name().c_str() << "iso = ";
        for (Index j = 1; j < hiso[i].nelem(); ++j) {
          if (missing == hiso[i][j])
            out3 << " "
                 << "m";
          else
            out3 << " "
                 << species_data[hspec[i]].Isotopologue()[hiso[i][j]].Name();
        }
        out3 << "\n";
      }
    }

    hinit = true;
  }

  // This contains the rest of the line to parse. At the beginning the
  // entire line. Line gets shorter and shorter as we continue to
  // extract stuff from the beginning.
  String line;

  // The first item is the molecule number:
  Index mo;

  // Look for more comments?
  bool comment = true;

  while (comment) {
    // Return true if eof is reached:
    if (is.eof()) return true;

    // Throw runtime_error if stream is bad:
    if (!is) throw std::runtime_error("Stream bad.");

    // Read line from file into linebuffer:
    getline(is, line);

    // It is possible that we were exactly at the end of the file before
    // calling getline. In that case the previous eof() was still false
    // because eof() evaluates only to true if one tries to read after the
    // end of the file. The following check catches this.
    if (line.nelem() == 0 && is.eof()) return true;

    // If the catalogue is in dos encoding, throw away the
    // additional carriage return
    if (line[line.nelem() - 1] == 13) {
      line.erase(line.nelem() - 1, 1);
    }

    // Because of the fixed FORTRAN format, we need to break up the line
    // explicitly in apropriate pieces. Not elegant, but works!

    // Extract molecule number:
    mo = 0;
    // Initialization of mo is important, because mo stays the same
    // if line is empty.
    extract(mo, line, 2);
    //      cout << "mo = " << mo << endl;

    // If mo == 0 this is just a comment line:
    if (0 != mo) {
      // See if we know this species. Exit with an error if the species is unknown.
      if (missing != hspec[mo]) {
        comment = false;

        // Check if data record has the right number of characters for the
        // in Hitran 1986-2001 format
        Index nChar = line.nelem() + 2;  // number of characters in data record;
        if (nChar != 100) {
          ostringstream os;
          os << "Invalid HITRAN 1986-2001 line data record with " << nChar
             << " characters (expected: 100)." << endl
             << line << " n: " << line.nelem();
          throw std::runtime_error(os.str());
        }

      } else {
        // See if this is already in warned_missing, use
        // std::count for that:
        if (0 == std::count(warned_missing.begin(), warned_missing.end(), mo)) {
          CREATE_OUT0;
          out0 << "Error: HITRAN mo = " << mo << " is not "
               << "known to ARTS.\n";
          warned_missing.push_back(mo);
        }
      }
    }
  }

  // Ok, we seem to have a valid species here.

  // Set mspecies from my cool index table:
  mqid.SetSpecies(hspec[mo]);

  // Extract isotopologue:
  Index iso;
  extract(iso, line, 1);
  //  cout << "iso = " << iso << endl;

  // Set misotopologue from the other cool index table.
  // We have to be careful to issue an error for unknown iso tags. Iso
  // could be either larger than the size of hiso[mo], or set
  // explicitly to missing. Unfortunately we have to test both cases.
  mqid.SetIsotopologue(missing);
  if (iso < hiso[mo].nelem())
    if (missing != hiso[mo][iso]) mqid.SetIsotopologue(hiso[mo][iso]);

  // Issue error message if misotopologue is still missing:
  if (missing == mqid.Isotopologue()) {
    ostringstream os;
    os << "Species: " << species_data[mqid.Species()].Name()
       << ", isotopologue iso = " << iso << " is unknown.";
    throw std::runtime_error(os.str());
  }

  // Position.
  {
    // HITRAN position in wavenumbers (cm^-1):
    Numeric v;
    // External constant from constants.cc:
    extern const Numeric SPEED_OF_LIGHT;
    // Conversion from wavenumber to Hz. If you multiply a line
    // position in wavenumber (cm^-1) by this constant, you get the
    // frequency in Hz.
    const Numeric w2Hz = SPEED_OF_LIGHT * 100.;

    // Extract HITRAN postion:
    extract(v, line, 12);

    // ARTS position in Hz:
    mf = v * w2Hz;
    //    cout << "mf = " << mf << endl;
  }

  // Intensity.
  {
    extern const Numeric SPEED_OF_LIGHT;  // in [m/s]

    // HITRAN intensity is in cm-1/(molec * cm-2) at 296 Kelvin.
    // It already includes the isotpic ratio.
    // The first cm-1 is the frequency unit (it cancels with the
    // 1/frequency unit of the line shape function).
    //
    // We need to do the following:
    // 1. Convert frequency from wavenumber to Hz (factor 1e2 * c).
    // 2. Convert [molec * cm-2] to [molec * m-2] (factor 1e-4).
    // 3. Take out the isotopologue ratio.

    const Numeric hi2arts = 1e-2 * SPEED_OF_LIGHT;

    Numeric s;
    if (line[6] == 'D') line[6] = 'E';
    // Extract HITRAN intensity:
    extract(s,
            line,
            10);  // NOTE:  If error shooting, FORTRAN "D" is not read properly.
    // Convert to ARTS units (Hz / (molec * m-2) ), or shorter: Hz*m^2
    mi0 = s * hi2arts;
    // Take out isotopologue ratio:
    mi0 /= species_data[mqid.Species()]
               .Isotopologue()[mqid.Isotopologue()]
               .Abundance();
  }

  // Skip transition probability:
  {
    Numeric r;
    extract(r, line, 10);
  }

  // Air broadening parameters.
  Numeric sgam, agam;
  {
    // HITRAN parameter is in cm-1/atm at 296 Kelvin
    // All parameters are HWHM (I hope this is true!)
    Numeric gam;
    // External constant from constants.cc: Converts atm to
    // Pa. Multiply value in atm by this number to get value in Pa.
    extern const Numeric ATM2PA;
    // External constant from constants.cc:
    extern const Numeric SPEED_OF_LIGHT;
    // Conversion from wavenumber to Hz. If you multiply a value in
    // wavenumber (cm^-1) by this constant, you get the value in Hz.
    const Numeric w2Hz = SPEED_OF_LIGHT * 1e2;
    // Ok, put together the end-to-end conversion that we need:
    const Numeric hi2arts = w2Hz / ATM2PA;

    // Extract HITRAN AGAM value:
    extract(gam, line, 5);

    // ARTS parameter in Hz/Pa:
    agam = gam * hi2arts;

    // Extract HITRAN SGAM value:
    extract(gam, line, 5);

    // ARTS parameter in Hz/Pa:
    sgam = gam * hi2arts;

    // If zero, set to agam:
    if (0 == sgam) sgam = agam;

    //    cout << "agam, sgam = " << magam << ", " << msgam << endl;
  }

  // Lower state energy.
  {
    // HITRAN parameter is in wavenumbers (cm^-1).
    // We have to convert this to the ARTS unit Joule.

    // Extract from Catalogue line
    extract(melow, line, 10);

    // Convert to Joule:
    melow = wavenumber_to_joule(melow);
  }

  // Temperature coefficient of broadening parameters.
  Numeric nair, nself;
  {
    // This is dimensionless, we can also extract directly.
    extract(nair, line, 4);

    // Set self broadening temperature coefficient to the same value:
    nself = nair;
    //    cout << "mnair = " << mnair << endl;
  }

  // Pressure shift.
  Numeric psf;
  {
    // HITRAN value in cm^-1 / atm. So the conversion goes exactly as
    // for the broadening parameters.
    Numeric d;
    // External constant from constants.cc: Converts atm to
    // Pa. Multiply value in atm by this number to get value in Pa.
    extern const Numeric ATM2PA;
    // External constant from constants.cc:
    extern const Numeric SPEED_OF_LIGHT;
    // Conversion from wavenumber to Hz. If you multiply a value in
    // wavenumber (cm^-1) by this constant, you get the value in Hz.
    const Numeric w2Hz = SPEED_OF_LIGHT * 1e2;
    // Ok, put together the end-to-end conversion that we need:
    const Numeric hi2arts = w2Hz / ATM2PA;

    // Extract HITRAN value:
    extract(d, line, 8);

    // ARTS value in Hz/Pa
    psf = d * hi2arts;
  }
  // Set the accuracies using the definition of HITRAN
  // indices. If some are missing, they are set to -1.

  //Skip upper state global quanta index
  {
    Index eu;
    extract(eu, line, 3);
  }

  //Skip lower state global quanta index
  {
    Index el;
    extract(el, line, 3);
  }

  //Skip upper state local quanta
  {
    Index eul;
    extract(eul, line, 9);
  }

  //Skip lower state local quanta
  {
    Index ell;
    if (species_data[mqid.Species()].Name() == "O2") {
      String helper = line.substr(0, 9);
      Index DJ = -helper.compare(3, 1, "Q");
      Index DN = -helper.compare(0, 1, "Q");
      Index N = atoi(helper.substr(1, 2).c_str());
      Index J = atoi(helper.substr(4, 2).c_str());

      mqid.LowerQuantumNumbers().Set(QuantumNumberType::N, N);
      mqid.LowerQuantumNumbers().Set(QuantumNumberType::J, J);
      mqid.UpperQuantumNumbers().Set(QuantumNumberType::N, N - DN);
      mqid.UpperQuantumNumbers().Set(QuantumNumberType::J, J - DJ);
    }

    extract(ell, line, 9);
  }

  // Accuracy index for frequency reference
  {
    Index df;
    // Extract HITRAN value:
    extract(df, line, 1);
  }

  // Accuracy index for intensity reference
  {
    Index di0;
    // Extract HITRAN value:
    extract(di0, line, 1);
  }

  // Accuracy index for halfwidth reference
  {
    Index dgam;
    // Extract HITRAN value:
    extract(dgam, line, 1);
  }

  // These were all the parameters that we can extract from
  // HITRAN. However, we still have to set the reference temperatures
  // to the appropriate value:

  // Reference temperature for Intensity in K.
  // (This is fix for HITRAN)
  mti0 = 296.0;

  // Skip four
  {
    Index four;
    extract(four, line, 4);
  }

  // This is the test for the last two characters of the line
  {
    /* 
     *   0 is nothing, 
     *  -1 is linemixing on the next line, 
     *  -3 is the non-resonant line 
     */
    Index test;
    extract(test, line, 2);
    //If the tag is as it should be, then a minus one means that more should be read
    if (test == -1 || test == -3)
      getline(is, line);
    else  // the line is done and we are happy to leave
    {
      mlineshapemodel = LineShape::Model(sgam, nself, agam, nair, psf);
      mstandard = true;
      return false;
    }
  }

  // In case we are unable to leave, the next line is a line mixing parameter line

  // First is the molecular number.  This should be the same as above.
  {
    Index mo2;
    extract(mo2, line, 2);
    // Skip one

    if (mo != mo2)
      throw std::runtime_error("There is an error in the line mixing\n");
  }

  Vector Y(4), G(4), T(4);

  // These are constants for AER but should be included because we need their grid.
  T[0] = 200;
  T[1] = 250;
  T[2] = 296;
  T[3] = 340;

  // Next is the Y  and G at various temperatures
  {
    Numeric Y_200K;
    extract(Y_200K, line, 13);
    Y[0] = Y_200K;
  }
  {
    Numeric G_200K;
    extract(G_200K, line, 11);
    G[0] = G_200K;
  }
  {
    Numeric Y_250K;
    extract(Y_250K, line, 13);
    Y[1] = Y_250K;
  }
  {
    Numeric G_250K;
    extract(G_250K, line, 11);
    G[1] = G_250K;
  }
  {
    Numeric Y_296K;
    extract(Y_296K, line, 13);
    Y[2] = Y_296K;
  }
  {
    Numeric G_296K;
    extract(G_296K, line, 11);
    G[2] = G_296K;
  }
  {
    Numeric Y_340K;
    extract(Y_340K, line, 13);
    Y[3] = Y_340K;
  }
  {
    Numeric G_340K;
    extract(G_340K, line, 11);
    G[3] = G_340K;
  }

  extern const Numeric ATM2PA;

  Y /= ATM2PA;
  G /= ATM2PA / ATM2PA;
  Y *=
      -1;  // ARTS uses (1-iY) as line-mixing factor, LBLRTM CO2 uses (1+iY), so we must change sign

  // Test that this is the end
  {
    Index test;
    extract(test, line, 2);
    if (test == -1) {
      mlineshapemodel = LineShape::Model(sgam,
                                         nself,
                                         agam,
                                         nair,
                                         psf,
                                         {T[0],
                                          T[1],
                                          T[2],
                                          T[3],
                                          Y[0],
                                          Y[1],
                                          Y[2],
                                          Y[3],
                                          G[0],
                                          G[1],
                                          G[2],
                                          G[3]});
      mstandard = true;

      return false;
    } else if (test == -3) {
      mlineshapemodel = LineShape::Model(sgam,
                                         nself,
                                         agam,
                                         nair,
                                         psf,
                                         {T[0],
                                          T[1],
                                          T[2],
                                          T[3],
                                          Y[0],
                                          Y[1],
                                          Y[2],
                                          Y[3],
                                          G[0],
                                          G[1],
                                          G[2],
                                          G[3]});
      mstandard = true;

      return false;
    } else
      return true;
  }
}

bool LineRecord::ReadFromHitran2004Stream(istream& is,
                                          const Verbosity& verbosity,
                                          const Numeric fmin) {
  CREATE_OUT3;

  // Global species lookup data:
  using global_data::species_data;

  // This value is used to flag missing data both in species and
  // isotopologue lists. Could be any number, it just has to be made sure
  // that it is neither the index of a species nor of an isotopologue.
  const Index missing = species_data.nelem() + 100;

  // We need a species index sorted by HITRAN tag. Keep this in a
  // static variable, so that we have to do this only once.  The ARTS
  // species index is hind[<HITRAN tag>].
  //
  // Allow for up to 100 species in HITRAN in the future.
  static Array<Index> hspec(100);

  // This is  an array of arrays for each hitran tag. It contains the
  // ARTS indices of the HITRAN isotopologues.
  static Array<ArrayOfIndex> hiso(100);

  // Remember if this stuff has already been initialized:
  static bool hinit = false;

  // Remember, about which missing species we have already issued a
  // warning:
  static ArrayOfIndex warned_missing;

  if (!hinit) {
    // Initialize hspec.
    // The value of missing means that we don't have this species.
    hspec = missing;  // Matpack can set all elements like this.

    for (Index i = 0; i < species_data.nelem(); ++i) {
      const SpeciesRecord& sr = species_data[i];

      // We have to be careful and check for the case that all
      // HITRAN isotopologue tags are -1 (this species is missing in HITRAN).

      if (sr.Isotopologue().nelem() && 0 < sr.Isotopologue()[0].HitranTag()) {
        // The HITRAN tags are stored as species plus isotopologue tags
        // (MO and ISO)
        // in the Isotopologue() part of the species record.
        // We can extract the MO part from any of the isotopologue tags,
        // so we use the first one. We do this by taking an integer
        // division by 10.

        Index mo = sr.Isotopologue()[0].HitranTag() / 10;
        //          cout << "mo = " << mo << endl;
        hspec[mo] = i;

        // Get a nicer to handle array of HITRAN iso tags:
        Index n_iso = sr.Isotopologue().nelem();
        ArrayOfIndex iso_tags;
        iso_tags.resize(n_iso);
        for (Index j = 0; j < n_iso; ++j) {
          iso_tags[j] = sr.Isotopologue()[j].HitranTag();
        }

        // Reserve elements for the isotopologue tags. How much do we
        // need? This depends on the largest HITRAN tag that we know
        // about!
        // Also initialize the tags to missing.
        //          cout << "iso_tags = " << iso_tags << endl;
        //          cout << "static_cast<Index>(max(iso_tags))%10 + 1 = "
        //               << static_cast<Index>(max(iso_tags))%10 + 1 << endl;
        hiso[mo].resize(max(iso_tags) % 10 + 1);
        hiso[mo] = missing;  // Matpack can set all elements like this.

        // Set the isotopologue tags:
        for (Index j = 0; j < n_iso; ++j) {
          if (0 < iso_tags[j])  // ignore -1 elements
          {
            // To get the iso tags from HitranTag() we also have to take
            // modulo 10 to get rid of mo.
            hiso[mo][iso_tags[j] % 10] = j;
          }
        }
      }
    }

    // Print the generated data structures (for debugging):
    out3 << "  HITRAN index table:\n";
    for (Index i = 0; i < hspec.nelem(); ++i) {
      if (missing != hspec[i]) {
        // The explicit conversion of Name to a c-String is
        // necessary, because setw does not work correctly for
        // stl Strings.
        out3 << "  mo = " << i << "   Species = " << setw(10)
             << setiosflags(ios::left) << species_data[hspec[i]].Name().c_str()
             << "iso = ";
        for (Index j = 1; j < hiso[i].nelem(); ++j) {
          if (missing == hiso[i][j])
            out3 << " "
                 << "m";
          else
            out3 << " "
                 << species_data[hspec[i]].Isotopologue()[hiso[i][j]].Name();
        }
        out3 << "\n";
      }
    }

    hinit = true;
  }

  // This contains the rest of the line to parse. At the beginning the
  // entire line. Line gets shorter and shorter as we continue to
  // extract stuff from the beginning.
  String line;

  // The first item is the molecule number:
  Index mo;

  // Look for more comments?
  bool comment = true;

  while (comment) {
    // Return true if eof is reached:
    if (is.eof()) return true;

    // Throw runtime_error if stream is bad:
    if (!is) throw runtime_error("Stream bad.");

    // Read line from file into linebuffer:
    getline(is, line);

    // It is possible that we were exactly at the end of the file before
    // calling getline. In that case the previous eof() was still false
    // because eof() evaluates only to true if one tries to read after the
    // end of the file. The following check catches this.
    if (line.nelem() == 0 && is.eof()) return true;

    // If the catalogue is in dos encoding, throw away the
    // additional carriage return
    if (line[line.nelem() - 1] == 13) {
      line.erase(line.nelem() - 1, 1);
    }

    // Because of the fixed FORTRAN format, we need to break up the line
    // explicitly in apropriate pieces. Not elegant, but works!

    // Extract molecule number:
    mo = 0;
    // Initialization of mo is important, because mo stays the same
    // if line is empty.
    extract(mo, line, 2);
    //      cout << "mo = " << mo << endl;

    // If mo == 0 this is just a comment line:
    if (0 != mo) {
      // See if we know this species.
      if (missing != hspec[mo]) {
        comment = false;

        // Check if data record has the right number of characters for the
        // in Hitran 2004 format
        Index nChar = line.nelem() + 2;  // number of characters in data record;
        if ((nChar == 161 && line[158] != ' ') || nChar > 161) {
          ostringstream os;
          os << "Invalid HITRAN 2004 line data record with " << nChar
             << " characters (expected: 160).";
          throw std::runtime_error(os.str());
        }

      } else {
        // See if this is already in warned_missing, use
        // std::count for that:
        if (0 == std::count(warned_missing.begin(), warned_missing.end(), mo)) {
          CREATE_OUT1;
          out1 << "Warning: HITRAN molecule number mo = " << mo << " is not "
               << "known to ARTS.\n";
          warned_missing.push_back(mo);
        }
      }
    }
  }

  // Ok, we seem to have a valid species here.

  // Set mspecies from my cool index table:
  mqid.SetSpecies(hspec[mo]);

  // Extract isotopologue:
  Index iso;
  extract(iso, line, 1);
  //  cout << "iso = " << iso << endl;

  // Set misotopologue from the other cool index table.
  // We have to be careful to issue an error for unknown iso tags. Iso
  // could be either larger than the size of hiso[mo], or set
  // explicitly to missing. Unfortunately we have to test both cases.
  mqid.SetIsotopologue(missing);
  if (iso < hiso[mo].nelem())
    if (missing != hiso[mo][iso]) mqid.SetIsotopologue(hiso[mo][iso]);

  // Issue error message if misotopologue is still missing:
  if (missing == mqid.Isotopologue()) {
    ostringstream os;
    os << "Species: " << species_data[mqid.Species()].Name()
       << ", isotopologue iso = " << iso << " is unknown.";
    throw std::runtime_error(os.str());
  }

  // Position.
  {
    // HITRAN position in wavenumbers (cm^-1):
    Numeric v;
    // External constant from constants.cc:
    extern const Numeric SPEED_OF_LIGHT;
    // Conversion from wavenumber to Hz. If you multiply a line
    // position in wavenumber (cm^-1) by this constant, you get the
    // frequency in Hz.
    const Numeric w2Hz = SPEED_OF_LIGHT * 100.;

    // Extract HITRAN postion:
    extract(v, line, 12);

    // ARTS position in Hz:
    mf = v * w2Hz;
    //    cout << "mf = " << mf << endl;
    if (mf < fmin) {
      mf = -1;
      return false;
    }
  }

  // Intensity.
  {
    extern const Numeric SPEED_OF_LIGHT;  // in [m/s]

    // HITRAN intensity is in cm-1/(molec * cm-2) at 296 Kelvin.
    // It already includes the isotpic ratio.
    // The first cm-1 is the frequency unit (it cancels with the
    // 1/frequency unit of the line shape function).
    //
    // We need to do the following:
    // 1. Convert frequency from wavenumber to Hz (factor 1e2 * c).
    // 2. Convert [molec * cm-2] to [molec * m-2] (factor 1e-4).
    // 3. Take out the isotopologue ratio.

    const Numeric hi2arts = 1e-2 * SPEED_OF_LIGHT;

    Numeric s;

    // Extract HITRAN intensity:
    extract(s, line, 10);
    // Convert to ARTS units (Hz / (molec * m-2) ), or shorter: Hz*m^2
    mi0 = s * hi2arts;
    // Take out isotopologue ratio:
    mi0 /= species_data[mqid.Species()]
               .Isotopologue()[mqid.Isotopologue()]
               .Abundance();
  }

  // Einstein coefficient
  {
    Numeric r;
    extract(r, line, 10);
    ma = r;
  }

  // Air broadening parameters.
  Numeric agam, sgam;
  {
    // HITRAN parameter is in cm-1/atm at 296 Kelvin
    // All parameters are HWHM (I hope this is true!)
    Numeric gam;
    // External constant from constants.cc: Converts atm to
    // Pa. Multiply value in atm by this number to get value in Pa.
    extern const Numeric ATM2PA;
    // External constant from constants.cc:
    extern const Numeric SPEED_OF_LIGHT;
    // Conversion from wavenumber to Hz. If you multiply a value in
    // wavenumber (cm^-1) by this constant, you get the value in Hz.
    const Numeric w2Hz = SPEED_OF_LIGHT * 1e2;
    // Ok, put together the end-to-end conversion that we need:
    const Numeric hi2arts = w2Hz / ATM2PA;

    // Extract HITRAN AGAM value:
    extract(gam, line, 5);

    // ARTS parameter in Hz/Pa:
    agam = gam * hi2arts;

    // Extract HITRAN SGAM value:
    extract(gam, line, 5);

    // ARTS parameter in Hz/Pa:
    sgam = gam * hi2arts;

    // If zero, set to agam:
    if (0 == sgam) sgam = agam;

    //    cout << "agam, sgam = " << magam << ", " << msgam << endl;
  }

  // Lower state energy.
  {
    // HITRAN parameter is in wavenumbers (cm^-1).
    // We have to convert this to the ARTS unit Joule.

    // Extract from Catalogue line
    extract(melow, line, 10);

    // Convert to Joule:
    melow = wavenumber_to_joule(melow);
  }

  // Temperature coefficient of broadening parameters.
  Numeric nair, nself;
  {
    // This is dimensionless, we can also extract directly.
    extract(nair, line, 4);

    // Set self broadening temperature coefficient to the same value:
    nself = nair;
    //    cout << "mnair = " << mnair << endl;
  }

  // Pressure shift.
  Numeric psf;
  {
    // HITRAN value in cm^-1 / atm. So the conversion goes exactly as
    // for the broadening parameters.
    Numeric d;
    // External constant from constants.cc: Converts atm to
    // Pa. Multiply value in atm by this number to get value in Pa.
    extern const Numeric ATM2PA;
    // External constant from constants.cc:
    extern const Numeric SPEED_OF_LIGHT;
    // Conversion from wavenumber to Hz. If you multiply a value in
    // wavenumber (cm^-1) by this constant, you get the value in Hz.
    const Numeric w2Hz = SPEED_OF_LIGHT * 1e2;
    // Ok, put together the end-to-end conversion that we need:
    const Numeric hi2arts = w2Hz / ATM2PA;

    // Extract HITRAN value:
    extract(d, line, 8);

    // ARTS value in Hz/Pa
    psf = d * hi2arts;
  }
  // Set the accuracies using the definition of HITRAN
  // indices. If some are missing, they are set to -1.

  static QuantumParserHITRAN2004 quantum_parser;
  const String qstr = line.substr(0, 15 * 4);

  // Upper state global quanta
  {
    Index eu;
    extract(eu, line, 15);
  }

  // Lower state global quanta
  {
    Index el;
    extract(el, line, 15);
  }

  // Upper state local quanta
  {
    Index eul;
    extract(eul, line, 15);
  }

  // Lower state local quanta
  {
    Index ell;
    extract(ell, line, 15);
  }

  // Parse quantum numbers.
  quantum_parser.Parse(mqid, qstr);

  // Accuracy index for frequency
  {
    Index df;
    // Extract HITRAN value:
    extract(df, line, 1);
  }

  // Accuracy index for intensity
  {
    Index di0;
    // Extract HITRAN value:
    extract(di0, line, 1);
  }

  // Accuracy index for air-broadened halfwidth
  {
    Index dgam;
    // Extract HITRAN value:
    extract(dgam, line, 1);
  }

  // Accuracy index for self-broadened half-width
  {
    Index dgam;
    // Extract HITRAN value:
    extract(dgam, line, 1);
  }

  // Accuracy index for temperature-dependence exponent for agam
  {
    Index dn;
    // Extract HITRAN value:
    extract(dn, line, 1);
  }

  // Accuracy index for pressure shift
  {
    Index dpsfi;
    // Extract HITRAN value (given in cm-1):
    extract(dpsfi, line, 1);
  }

  // These were all the parameters that we can extract from
  // HITRAN 2004. However, we still have to set the reference temperatures
  // to the appropriate value:

  // Reference temperature for Intensity in K.
  mti0 = 296.0;

  // Set line shape computer
  mlineshapemodel = LineShape::Model(sgam, nself, agam, nair, psf);
  mstandard = true;
  {
    Index garbage;
    extract(garbage, line, 13);

    // The statistical weights
    extract(mgupper, line, 7);
    extract(mglower, line, 7);
  }

  // That's it!
  return false;
}

bool LineRecord::ReadFromMytran2Stream(istream& is,
                                       const Verbosity& verbosity) {
  CREATE_OUT3;

  // Global species lookup data:
  using global_data::species_data;

  // This value is used to flag missing data both in species and
  // isotopologue lists. Could be any number, it just has to be made sure
  // that it is neither the index of a species nor of an isotopologue.
  const Index missing = species_data.nelem() + 100;

  // We need a species index sorted by MYTRAN tag. Keep this in a
  // static variable, so that we have to do this only once.  The ARTS
  // species index is hind[<MYTRAN tag>]. The value of
  // missing means that we don't have this species.
  //
  // Allow for up to 100 species in MYTRAN in the future.
  static Array<Index> hspec(100, missing);

  // This is  an array of arrays for each mytran tag. It contains the
  // ARTS indices of the MYTRAN isotopologues.
  static Array<ArrayOfIndex> hiso(100);

  // Remember if this stuff has already been initialized:
  static bool hinit = false;

  // Remember, about which missing species we have already issued a
  // warning:
  static ArrayOfIndex warned_missing;

  if (!hinit) {
    for (Index i = 0; i < species_data.nelem(); ++i) {
      const SpeciesRecord& sr = species_data[i];

      // We have to be careful and check for the case that all
      // MYTRAN isotopologue tags are -1 (this species is missing in MYTRAN).

      if (0 < sr.Isotopologue()[0].MytranTag()) {
        // The MYTRAN tags are stored as species plus isotopologue tags
        // (MO and ISO)
        // in the Isotopologue() part of the species record.
        // We can extract the MO part from any of the isotopologue tags,
        // so we use the first one. We do this by taking an integer
        // division by 10.

        Index mo = sr.Isotopologue()[0].MytranTag() / 10;
        //          cout << "mo = " << mo << endl;
        hspec[mo] = i;

        // Get a nicer to handle array of MYTRAN iso tags:
        Index n_iso = sr.Isotopologue().nelem();
        ArrayOfIndex iso_tags;
        iso_tags.resize(n_iso);
        for (Index j = 0; j < n_iso; ++j) {
          iso_tags[j] = sr.Isotopologue()[j].MytranTag();
        }

        // Reserve elements for the isotopologue tags. How much do we
        // need? This depends on the largest MYTRAN tag that we know
        // about!
        // Also initialize the tags to missing.
        //          cout << "iso_tags = " << iso_tags << endl;
        //          cout << "static_cast<Index>(max(iso_tags))%10 + 1 = "
        //               << static_cast<Index>(max(iso_tags))%10 + 1 << endl;
        hiso[mo].resize(max(iso_tags) % 10 + 1);
        hiso[mo] = missing;  // Matpack can set all elements like this.

        // Set the isotopologue tags:
        for (Index j = 0; j < n_iso; ++j) {
          if (0 < iso_tags[j])  // ignore -1 elements
          {
            // To get the iso tags from MytranTag() we also have to take
            // modulo 10 to get rid of mo.
            //                  cout << "iso_tags[j] % 10 = " << iso_tags[j] % 10 << endl;
            hiso[mo][iso_tags[j] % 10] = j;
          }
        }
      }
    }

    //      cout << "hiso = " << hiso << endl << "***********" << endl;

    // Print the generated data structures (for debugging):
    out3 << "  MYTRAN index table:\n";
    for (Index i = 0; i < hspec.nelem(); ++i) {
      if (missing != hspec[i]) {
        // The explicit conversion of Name to a c-String is
        // necessary, because setw does not work correctly for
        // stl Strings.
        out3 << "  mo = " << i << "   Species = " << setw(10)
             << setiosflags(ios::left) << species_data[hspec[i]].Name().c_str()
             << "iso = ";
        for (Index j = 1; j < hiso[i].nelem(); ++j) {
          if (missing == hiso[i][j])
            out3 << " "
                 << "m";
          else
            out3 << " "
                 << species_data[hspec[i]].Isotopologue()[hiso[i][j]].Name();
        }
        out3 << "\n";
      }
    }

    hinit = true;
  }

  // This contains the rest of the line to parse. At the beginning the
  // entire line. Line gets shorter and shorter as we continue to
  // extract stuff from the beginning.
  String line;

  // The first item is the molecule number:
  Index mo;

  // Look for more comments?
  bool comment = true;

  while (comment) {
    // Return true if eof is reached:
    if (is.eof()) return true;

    // Throw runtime_error if stream is bad:
    if (!is) throw runtime_error("Stream bad.");

    // Read line from file into linebuffer:
    getline(is, line);

    // It is possible that we were exactly at the end of the file before
    // calling getline. In that case the previous eof() was still false
    // because eof() evaluates only to true if one tries to read after the
    // end of the file. The following check catches this.
    if (line.nelem() == 0 && is.eof()) return true;

    // Because of the fixed FORTRAN format, we need to break up the line
    // explicitly in apropriate pieces. Not elegant, but works!

    // Extract molecule number:
    mo = 0;
    // Initialization of mo is important, because mo stays the same
    // if line is empty.
    extract(mo, line, 2);
    //           cout << "mo = " << mo << endl;

    // If mo == 0 this is just a comment line:
    if (0 != mo) {
      // See if we know this species. We will give an error if a
      // species is not known.
      if (missing != hspec[mo])
        comment = false;
      else {
        // See if this is already in warned_missing, use
        // std::count for that:
        if (0 == std::count(warned_missing.begin(), warned_missing.end(), mo)) {
          CREATE_OUT0;
          out0 << "Error: MYTRAN mo = " << mo << " is not "
               << "known to ARTS.\n";
          warned_missing.push_back(mo);
        }
      }
    }
  }

  // Ok, we seem to have a valid species here.

  // Set mspecies from my cool index table:
  mqid.SetSpecies(hspec[mo]);

  // Extract isotopologue:
  Index iso;
  extract(iso, line, 1);
  //  cout << "iso = " << iso << endl;

  // Set misotopologue from the other cool index table.
  // We have to be careful to issue an error for unknown iso tags. Iso
  // could be either larger than the size of hiso[mo], or set
  // explicitly to missing. Unfortunately we have to test both cases.
  mqid.SetIsotopologue(missing);
  if (iso < hiso[mo].nelem())
    if (missing != hiso[mo][iso]) mqid.SetIsotopologue(hiso[mo][iso]);

  // Issue error message if misotopologue is still missing:
  if (missing == mqid.Isotopologue()) {
    ostringstream os;
    os << "Species: " << species_data[mqid.Species()].Name()
       << ", isotopologue iso = " << iso << " is unknown.";
    throw runtime_error(os.str());
  }

  // Position.
  {
    // MYTRAN position in MHz:
    Numeric v;

    // Extract MYTRAN postion:
    extract(v, line, 13);

    // ARTS position in Hz:
    mf = v * 1E6;
    //    cout << "mf = " << mf << endl;
  }

  // Accuracy for line position
  {
    // Extract MYTRAN postion accuracy:
    Numeric df;
    extract(df, line, 8);
  }

  // Intensity.
  {
    extern const Numeric SPEED_OF_LIGHT;  // in [m/s]

    // MYTRAN2 intensity is in cm-1/(molec * cm-2) at 296 Kelvin.
    // (just like HITRAN, only isotopologue ratio is not included)
    // The first cm-1 is the frequency unit (it cancels with the
    // 1/frequency unit of the line shape function).
    //
    // We need to do the following:
    // 1. Convert frequency from wavenumber to Hz (factor 1e2 * c)
    // 2. Convert [molec * cm-2] to [molec * m-2] (factor 1e-4)

    const Numeric hi2arts = 1e-2 * SPEED_OF_LIGHT;

    Numeric s;

    // Extract HITRAN intensity:
    extract(s, line, 10);

    // Convert to ARTS units (Hz / (molec * m-2) ), or shorter: Hz*m^2
    mi0 = s * hi2arts;
  }

  // Air broadening parameters.
  Numeric agam, sgam;
  {
    // MYTRAN parameter is in MHz/Torr at reference temperature
    // All parameters are HWHM
    Numeric gam;
    // External constant from constants.cc: Converts torr to
    // Pa. Multiply value in torr by this number to get value in Pa.
    extern const Numeric TORR2PA;

    // Extract HITRAN AGAM value:
    extract(gam, line, 5);

    // ARTS parameter in Hz/Pa:
    agam = gam * 1E6 / TORR2PA;

    // Extract MYTRAN SGAM value:
    extract(gam, line, 5);

    // ARTS parameter in Hz/Pa:
    sgam = gam * 1E6 / TORR2PA;

    //    cout << "agam, sgam = " << magam << ", " << msgam << endl;
  }

  // Lower state energy.
  {
    // MYTRAN parameter is in wavenumbers (cm^-1).
    // We have to convert this to the ARTS unit Joule.

    // Extract from Catalogue line
    extract(melow, line, 10);

    // Convert to Joule:
    melow = wavenumber_to_joule(melow);
  }

  // Temperature coefficient of broadening parameters.
  Numeric nself, nair;
  {
    // This is dimensionless, we can also extract directly.
    extract(nair, line, 4);

    // Extract the self broadening parameter:
    extract(nself, line, 4);
    //    cout << "mnair = " << mnair << endl;
  }

  // Reference temperature for broadening parameter in K:
  Numeric tgam;
  {
    // correct units, extract directly
    extract(tgam, line, 7);
  }

  // Pressure shift.
  Numeric psf;
  {
    // MYTRAN value in MHz/Torr
    Numeric d;
    // External constant from constants.cc: Converts torr to
    // Pa. Multiply value in torr by this number to get value in Pa.
    extern const Numeric TORR2PA;

    // Extract MYTRAN value:
    extract(d, line, 9);

    // ARTS value in Hz/Pa
    psf = d * 1E6 / TORR2PA;
  }
  // Set the accuracies using the definition of MYTRAN accuracy
  // indices. If some are missing, they are set to -1.

  //Skip upper state global quanta index
  {
    Index eu;
    extract(eu, line, 3);
  }

  //Skip lower state global quanta index
  {
    Index el;
    extract(el, line, 3);
  }

  //Skip upper state local quanta
  {
    Index eul;
    extract(eul, line, 9);
  }

  //Skip lower state local quanta
  {
    Index ell;
    extract(ell, line, 9);
  }
  // Accuracy index for intensity
  {
    Index di0;
    // Extract MYTRAN value:
    extract(di0, line, 1);
  }

  // Accuracy index for AGAM
  {
    Index dgam;
    // Extract MYTRAN value:
    extract(dgam, line, 1);
  }

  // Accuracy index for NAIR
  {
    Index dair;
    // Extract MYTRAN value:
    extract(dair, line, 1);
  }

  // These were all the parameters that we can extract from
  // MYTRAN. However, we still have to set the reference temperatures
  // to the appropriate value:

  // Reference temperature for Intensity in K.
  // (This is fix for MYTRAN2)
  mti0 = 296.0;

  // It is important that you intialize here all the new parameters that
  // you added to the line record. (This applies to all the reading
  // functions, also for ARTS, JPL, and HITRAN format.) Parameters
  // should be either set from the catalogue, or set to -1.)

  // Convert to correct temperature if tgam != ti0
  if (tgam != mti0) {
    agam *= pow(tgam / mti0, nair);
    sgam *= pow(tgam / mti0, nself);
    psf *= pow(tgam / mti0, (Numeric).25 + (Numeric)1.5 * nair);
  }

  // Set line shape computer
  mlineshapemodel = LineShape::Model(sgam, nself, agam, nair, psf);
  mstandard = true;

  // That's it!
  return false;
}

bool LineRecord::ReadFromJplStream(istream& is, const Verbosity& verbosity) {
  CREATE_OUT3;

  // Global species lookup data:
  using global_data::species_data;

  // We need a species index sorted by JPL tag. Keep this in a
  // static variable, so that we have to do this only once.  The ARTS
  // species index is JplMap[<JPL tag>]. We need Index in this map,
  // because the tag array within the species data is an Index array.
  static map<Index, SpecIsoMap> JplMap;

  // Remember if this stuff has already been initialized:
  static bool hinit = false;

  if (!hinit) {
    out3 << "  JPL index table:\n";

    for (Index i = 0; i < species_data.nelem(); ++i) {
      const SpeciesRecord& sr = species_data[i];

      for (Index j = 0; j < sr.Isotopologue().nelem(); ++j) {
        for (Index k = 0; k < sr.Isotopologue()[j].JplTags().nelem(); ++k) {
          SpecIsoMap indicies(i, j);

          JplMap[sr.Isotopologue()[j].JplTags()[k]] = indicies;

          // Print the generated data structures (for debugging):
          // The explicit conversion of Name to a c-String is
          // necessary, because setw does not work correctly for
          // stl Strings.
          const Index& i1 =
              JplMap[sr.Isotopologue()[j].JplTags()[k]].Speciesindex();
          const Index& i2 =
              JplMap[sr.Isotopologue()[j].JplTags()[k]].Isotopologueindex();

          out3 << "  JPL TAG = " << sr.Isotopologue()[j].JplTags()[k]
               << "   Species = " << setw(10) << setiosflags(ios::left)
               << species_data[i1].Name().c_str()
               << "iso = " << species_data[i1].Isotopologue()[i2].Name().c_str()
               << "\n";
        }
      }
    }
    hinit = true;
  }

  // This contains the rest of the line to parse. At the beginning the
  // entire line. Line gets shorter and shorter as we continue to
  // extract stuff from the beginning.
  String line;

  // Look for more comments?
  bool comment = true;

  while (comment) {
    // Return true if eof is reached:
    if (is.eof()) return true;

    // Throw runtime_error if stream is bad:
    if (!is) throw runtime_error("Stream bad.");

    // Read line from file into linebuffer:
    getline(is, line);

    // It is possible that we were exactly at the end of the file before
    // calling getline. In that case the previous eof() was still false
    // because eof() evaluates only to true if one tries to read after the
    // end of the file. The following check catches this.
    if (line.nelem() == 0 && is.eof()) return true;

    // Because of the fixed FORTRAN format, we need to break up the line
    // explicitly in apropriate pieces. Not elegant, but works!

    // Extract center frequency:
    // Initialization of v is important, because v stays the same
    // if line is empty.
    // JPL position in MHz:
    Numeric v = 0.0;

    // Extract JPL position:
    extract(v, line, 13);

    // check for empty line
    if (v != 0.0) {
      // ARTS position in Hz:
      mf = v * 1E6;

      comment = false;
    }
  }

  // Accuracy for line position
  {
    Numeric df;
    extract(df, line, 8);
  }

  // Intensity.
  {
    // JPL has log (10) of intensity in nm2 MHz at 300 Kelvin.
    //
    // We need to do the following:
    // 1. take 10^intensity
    // 2. convert to cm-1/(molecule * cm-2): devide by c * 1e10
    // 3. Convert frequency from wavenumber to Hz (factor 1e2 * c)
    // 4. Convert [molec * cm-2] to [molec * m-2] (factor 1e-4)

    Numeric s;

    // Extract JPL intensity:
    extract(s, line, 8);

    // remove log
    s = pow((Numeric)10., s);

    // Convert to ARTS units (Hz / (molec * m-2) ), or shorter: Hz*m^2
    mi0 = s / 1E12;
  }

  // Degrees of freedom
  {
    Index dr;

    // Extract degrees of freedom
    extract(dr, line, 2);
  }

  // Lower state energy.
  {
    // JPL parameter is in wavenumbers (cm^-1).
    // We have to convert this to the ARTS unit Joule.

    // Extract from Catalogue line
    extract(melow, line, 10);

    // Convert to Joule:
    melow = wavenumber_to_joule(melow);
  }

  // Upper state degeneracy
  {
    Index gup;

    // Extract upper state degeneracy
    extract(gup, line, 3);
  }

  // Tag number
  Index tag;
  {
    // Extract Tag number
    extract(tag, line, 7);

    // make sure tag is not negative (damned jpl cat):
    tag = tag > 0 ? tag : -tag;
  }

  // ok, now for the cool index map:

  // is this tag valid?
  const map<Index, SpecIsoMap>::const_iterator i = JplMap.find(tag);
  if (i == JplMap.end()) {
    ostringstream os;
    os << "JPL Tag: " << tag << " is unknown.";
    throw runtime_error(os.str());
  }

  SpecIsoMap id = i->second;

  // Set line ID
  mqid.SetSpecies(id.Speciesindex());
  mqid.SetIsotopologue(id.Isotopologueindex());

  // Air broadening parameters: unknown to jpl, use old iup forward
  // model default values, which is mostly set to 0.0025 GHz/hPa, even
  // though for some lines the pressure broadening is given explicitly
  // in the program code. The explicitly given values are ignored and
  // only the default value is set. Self broadening was in general not
  // considered in the old forward model.
  Numeric agam, sgam;
  {
    // ARTS parameter in Hz/Pa:
    agam = 2.5E4;

    // ARTS parameter in Hz/Pa:
    sgam = agam;
  }

  // Temperature coefficient of broadening parameters. Was set to 0.75
  // in old forward model, even though for some lines the parameter is
  // given explicitly in the program code. The explicitly given values
  // are ignored and only the default value is set. Self broadening
  // not considered.
  Numeric nair, nself;
  {
    nair = 0.75;
    nself = 0.0;
  }

  // Reference temperature for broadening parameter in K, was
  // generally set to 300 K in old forward model, with the exceptions
  // as already mentioned above: //DEPRECEATED but is same as for mti0 so moving on
  //   {
  //     mtgam = 300.0;
  //   }

  // Pressure shift: not given in JPL, set to 0
  Numeric psf;
  { psf = 0.0; }

  // These were all the parameters that we can extract from
  // JPL. However, we still have to set the reference temperatures
  // to the appropriate value:

  // Reference temperature for Intensity in K.
  mti0 = 300.0;

  // Set line shape computer
  mlineshapemodel = LineShape::Model(sgam, nself, agam, nair, psf);
  mstandard = true;

  // That's it!
  return false;
}

bool LineRecord::ReadFromArtscat3Stream(istream& is,
                                        const Verbosity& verbosity) {
  CREATE_OUT3;

  // Global species lookup data:
  using global_data::species_data;

  // We need a species index sorted by Arts identifier. Keep this in a
  // static variable, so that we have to do this only once.  The ARTS
  // species index is ArtsMap[<Arts String>].
  static map<String, SpecIsoMap> ArtsMap;

  // Remember if this stuff has already been initialized:
  static bool hinit = false;

  mversion = 5;

  if (!hinit) {
    out3 << "  ARTS index table:\n";

    for (Index i = 0; i < species_data.nelem(); ++i) {
      const SpeciesRecord& sr = species_data[i];

      for (Index j = 0; j < sr.Isotopologue().nelem(); ++j) {
        SpecIsoMap indicies(i, j);
        String buf = sr.Name() + "-" + sr.Isotopologue()[j].Name();

        ArtsMap[buf] = indicies;

        // Print the generated data structures (for debugging):
        // The explicit conversion of Name to a c-String is
        // necessary, because setw does not work correctly for
        // stl Strings.
        const Index& i1 = ArtsMap[buf].Speciesindex();
        const Index& i2 = ArtsMap[buf].Isotopologueindex();

        out3 << "  Arts Identifier = " << buf << "   Species = " << setw(10)
             << setiosflags(ios::left) << species_data[i1].Name().c_str()
             << "iso = " << species_data[i1].Isotopologue()[i2].Name().c_str()
             << "\n";
      }
    }
    hinit = true;
  }

  // This always contains the rest of the line to parse. At the
  // beginning the entire line. Line gets shorter and shorter as we
  // continue to extract stuff from the beginning.
  String line;

  // Look for more comments?
  bool comment = true;

  while (comment) {
    // Return true if eof is reached:
    if (is.eof()) return true;

    // Throw runtime_error if stream is bad:
    if (!is) throw runtime_error("Stream bad.");

    // Read line from file into linebuffer:
    getline(is, line);

    // It is possible that we were exactly at the end of the file before
    // calling getline. In that case the previous eof() was still false
    // because eof() evaluates only to true if one tries to read after the
    // end of the file. The following check catches this.
    if (line.nelem() == 0 && is.eof()) return true;

    // @ as first character marks catalogue entry
    char c;
    extract(c, line, 1);

    // check for empty line
    if (c == '@') {
      comment = false;
    }
  }

  // read the arts identifier String
  istringstream icecream(line);

  String artsid;
  icecream >> artsid;

  if (artsid.length() != 0) {
    // ok, now for the cool index map:
    // is this arts identifier valid?
    const map<String, SpecIsoMap>::const_iterator i = ArtsMap.find(artsid);
    if (i == ArtsMap.end()) {
      ostringstream os;
      os << "ARTS Tag: " << artsid << " is unknown.";
      throw runtime_error(os.str());
    }

    SpecIsoMap id = i->second;

    // Set mspecies:
    mqid.SetSpecies(id.Speciesindex());

    // Set misotopologue:
    mqid.SetIsotopologue(id.Isotopologueindex());

    // Extract center frequency:
    icecream >> mf;

    Numeric psf;
    // Extract pressure shift:
    icecream >> psf;

    // Extract intensity:
    icecream >> mi0;

    // Extract reference temperature for Intensity in K:
    icecream >> mti0;

    // Extract lower state energy:
    icecream >> melow;

    // Extract air broadening parameters:
    Numeric agam, sgam;
    icecream >> agam;
    icecream >> sgam;

    // Extract temperature coefficient of broadening parameters:
    Numeric nair, nself;
    icecream >> nair;
    icecream >> nself;

    // Extract reference temperature for broadening parameter in K:
    Numeric tgam;
    icecream >> tgam;

    // Extract the aux parameters:
    Index naux;
    icecream >> naux;

    // resize the aux array and read it
    ArrayOfNumeric maux;
    maux.resize(naux);

    for (Index j = 0; j < naux; j++) {
      icecream >> maux[j];
      //cout << "maux" << j << " = " << maux[j] << "\n";
    }

    // Extract accuracies:
    Numeric dagam, dsgam, dnair, dnself, dpsf;
    try {
      Numeric mdf, mdi0;
      icecream >> mdf;
      icecream >> mdi0;
      icecream >> dagam;
      icecream >> dsgam;
      icecream >> dnair;
      icecream >> dnself;
      icecream >> dpsf;
    } catch (const std::runtime_error&) {
      // Nothing to do here, the accuracies are optional, so we
      // just set them to -1 and continue reading the next line of
      // the catalogue
      dagam = -1;
      dsgam = -1;
      dnair = -1;
      dnself = -1;
      dpsf = -1;
    }

    // Fix if tgam is different from ti0
    if (tgam != mti0) {
      agam = agam * pow(tgam / mti0, nair);
      sgam = sgam * pow(tgam / mti0, nself);
      psf = psf * pow(tgam / mti0, (Numeric).25 + (Numeric)1.5 * nair);
    }

    // Set line shape computer
    mlineshapemodel = LineShape::Model(sgam, nself, agam, nair, psf);
    mstandard = true;
  }

  // That's it!
  return false;
}

bool LineRecord::ReadFromArtscat4Stream(istream& is,
                                        const Verbosity& verbosity) {
  CREATE_OUT3;

  // Global species lookup data:
  using global_data::species_data;

  // We need a species index sorted by Arts identifier. Keep this in a
  // static variable, so that we have to do this only once.  The ARTS
  // species index is ArtsMap[<Arts String>].
  static map<String, SpecIsoMap> ArtsMap;

  // Remember if this stuff has already been initialized:
  static bool hinit = false;

  mversion = 5;

  if (!hinit) {
    out3 << "  ARTS index table:\n";

    for (Index i = 0; i < species_data.nelem(); ++i) {
      const SpeciesRecord& sr = species_data[i];

      for (Index j = 0; j < sr.Isotopologue().nelem(); ++j) {
        SpecIsoMap indicies(i, j);
        String buf = sr.Name() + "-" + sr.Isotopologue()[j].Name();

        ArtsMap[buf] = indicies;

        // Print the generated data structures (for debugging):
        // The explicit conversion of Name to a c-String is
        // necessary, because setw does not work correctly for
        // stl Strings.
        const Index& i1 = ArtsMap[buf].Speciesindex();
        const Index& i2 = ArtsMap[buf].Isotopologueindex();

        out3 << "  Arts Identifier = " << buf << "   Species = " << setw(10)
             << setiosflags(ios::left) << species_data[i1].Name().c_str()
             << "iso = " << species_data[i1].Isotopologue()[i2].Name().c_str()
             << "\n";
      }
    }
    hinit = true;
  }

  // This always contains the rest of the line to parse. At the
  // beginning the entire line. Line gets shorter and shorter as we
  // continue to extract stuff from the beginning.
  String line;

  // Look for more comments?
  bool comment = true;

  while (comment) {
    // Return true if eof is reached:
    if (is.eof()) return true;

    // Throw runtime_error if stream is bad:
    if (!is) throw runtime_error("Stream bad.");

    // Read line from file into linebuffer:
    getline(is, line);

    // It is possible that we were exactly at the end of the file before
    // calling getline. In that case the previous eof() was still false
    // because eof() evaluates only to true if one tries to read after the
    // end of the file. The following check catches this.
    if (line.nelem() == 0 && is.eof()) return true;

    // @ as first character marks catalogue entry
    char c;
    extract(c, line, 1);

    // check for empty line
    if (c == '@') {
      comment = false;
    }
  }

  // read the arts identifier String
  istringstream icecream(line);

  String artsid;
  icecream >> artsid;

  if (artsid.length() != 0) {
    // ok, now for the cool index map:
    // is this arts identifier valid?
    const map<String, SpecIsoMap>::const_iterator i = ArtsMap.find(artsid);
    if (i == ArtsMap.end()) {
      ostringstream os;
      os << "ARTS Tag: " << artsid << " is unknown.";
      throw runtime_error(os.str());
    }

    SpecIsoMap id = i->second;

    // Set line ID
    mqid.SetSpecies(id.Speciesindex());
    mqid.SetIsotopologue(id.Isotopologueindex());

    // Extract center frequency:
    icecream >> mf;

    // Extract intensity:
    icecream >> mi0;

    // Extract reference temperature for Intensity in K:
    icecream >> mti0;

    // Extract lower state energy:
    icecream >> melow;

    // Extract Einstein A-coefficient:
    icecream >> ma;

    // Extract upper state stat. weight:
    icecream >> mgupper;

    // Extract lower state stat. weight:
    icecream >> mglower;

    LineShape::from_artscat4(icecream, mlineshapemodel, mqid);
    mstandard = true;

    // Remaining entries are the quantum numbers
    String mquantum_numbers_str;
    getline(icecream, mquantum_numbers_str);

    mquantum_numbers_str.trim();
    // FIXME: OLE: Added this if to catch crash for species like CO, PH3
    // where the line in the catalog is too short. Better would be to
    // only read the n and j for Zeeman species, but we don't have that
    // information here

    if (species_data[mqid.Species()].Name() == "SO") {
      // Note that atoi converts *** to 0.
      mqid.UpperQuantumNumbers().Set(
          QuantumNumberType::N,
          Rational(atoi(mquantum_numbers_str.substr(0, 3).c_str())));
      mqid.LowerQuantumNumbers().Set(
          QuantumNumberType::N,
          Rational(atoi(mquantum_numbers_str.substr(6, 3).c_str())));
      mqid.UpperQuantumNumbers().Set(
          QuantumNumberType::J,
          Rational(atoi(mquantum_numbers_str.substr(3, 3).c_str())));
      mqid.LowerQuantumNumbers().Set(
          QuantumNumberType::J,
          Rational(atoi(mquantum_numbers_str.substr(9, 3).c_str())));
    }

    if (mquantum_numbers_str.nelem() >= 25) {
      if (species_data[mqid.Species()].Name() == "O2") {
        String vstr = mquantum_numbers_str.substr(0, 3);
        ArrayOfIndex v(3);
        for (Index vi = 0; vi < 3; vi++) {
          if (vstr[0] != ' ')
            extract(v[vi], vstr, 1);
          else
            v[vi] = -1;
        }

        if (v[2] > -1) {
          mqid.UpperQuantumNumbers().Set(QuantumNumberType::v1, Rational(v[2]));
          mqid.LowerQuantumNumbers().Set(QuantumNumberType::v1, Rational(v[2]));
        }

        String qstr1 = mquantum_numbers_str.substr(4, 12);
        String qstr2 = mquantum_numbers_str.substr(4 + 12 + 1, 12);
        ArrayOfIndex q(6);
        for (Index qi = 0; qi < 3; qi++) {
          if (qstr1.substr(0, 4) != "    ")
            extract(q[qi], qstr1, 4);
          else
            q[qi] = -1;
        }
        for (Index qi = 3; qi < 6; qi++) {
          if (qstr2.substr(0, 4) != "    ")
            extract(q[qi], qstr2, 4);
          else
            q[qi] = -1;
        }

        if (q[0] > -1)
          mqid.UpperQuantumNumbers().Set(QuantumNumberType::N, Rational(q[0]));
        if (q[1] > -1)
          mqid.UpperQuantumNumbers().Set(QuantumNumberType::J, Rational(q[1]));
        if (q[2] > -1)
          mqid.UpperQuantumNumbers().Set(QuantumNumberType::F,
                                         q[2] - Rational(1, 2));
        if (q[3] > -1)
          mqid.LowerQuantumNumbers().Set(QuantumNumberType::N, Rational(q[3]));
        if (q[4] > -1)
          mqid.LowerQuantumNumbers().Set(QuantumNumberType::J, Rational(q[4]));
        if (q[5] > -1)
          mqid.LowerQuantumNumbers().Set(QuantumNumberType::F,
                                         q[5] - Rational(1, 2));
      }
    }
  }

  // That's it!
  return false;
}

bool LineRecord::ReadFromArtscat5Stream(istream& is,
                                        const Verbosity& verbosity) {
  CREATE_OUT3;

  // Global species lookup data:
  using global_data::species_data;

  // We need a species index sorted by Arts identifier. Keep this in a
  // static variable, so that we have to do this only once.  The ARTS
  // species index is ArtsMap[<Arts String>].
  static map<String, SpecIsoMap> ArtsMap;

  // Remember if this stuff has already been initialized:
  static bool hinit = false;

  mversion = 5;

  LineShape::Model line_mixing_model;
  bool lmd_found = false;

  if (!hinit) {
    out3 << "  ARTS index table:\n";

    for (Index i = 0; i < species_data.nelem(); ++i) {
      const SpeciesRecord& sr = species_data[i];

      for (Index j = 0; j < sr.Isotopologue().nelem(); ++j) {
        SpecIsoMap indicies(i, j);
        String buf = sr.Name() + "-" + sr.Isotopologue()[j].Name();

        ArtsMap[buf] = indicies;

        // Print the generated data structures (for debugging):
        // The explicit conversion of Name to a c-String is
        // necessary, because setw does not work correctly for
        // stl Strings.
        const Index& i1 = ArtsMap[buf].Speciesindex();
        const Index& i2 = ArtsMap[buf].Isotopologueindex();

        out3 << "  Arts Identifier = " << buf << "   Species = " << setw(10)
             << setiosflags(ios::left) << species_data[i1].Name().c_str()
             << "iso = " << species_data[i1].Isotopologue()[i2].Name().c_str()
             << "\n";
      }
    }
    hinit = true;
  }

  // This always contains the rest of the line to parse. At the
  // beginning the entire line. Line gets shorter and shorter as we
  // continue to extract stuff from the beginning.
  String line;

  // Look for more comments?
  bool comment = true;

  while (comment) {
    // Return true if eof is reached:
    if (is.eof()) return true;

    // Throw runtime_error if stream is bad:
    if (!is) throw runtime_error("Stream bad.");

    // Read line from file into linebuffer:
    getline(is, line);

    // It is possible that we were exactly at the end of the file before
    // calling getline. In that case the previous eof() was still false
    // because eof() evaluates only to true if one tries to read after the
    // end of the file. The following check catches this.
    if (line.nelem() == 0 && is.eof()) return true;

    // @ as first character marks catalogue entry
    char c;
    extract(c, line, 1);

    // check for empty line
    if (c == '@') {
      comment = false;
    }
  }

  // read the arts identifier String
  istringstream icecream(line);

  try {
    String artsid;
    icecream >> artsid;

    if (artsid.length() != 0) {
      // ok, now for the cool index map:
      // is this arts identifier valid?
      const map<String, SpecIsoMap>::const_iterator i = ArtsMap.find(artsid);
      if (i == ArtsMap.end()) {
        ostringstream os;
        os << "ARTS Tag: " << artsid << " is unknown.";
        throw runtime_error(os.str());
      }

      SpecIsoMap id = i->second;

      // Set line ID:
      mqid.SetSpecies(id.Speciesindex());
      mqid.SetIsotopologue(id.Isotopologueindex());

      // Extract center frequency:
      icecream >> double_imanip() >> mf;

      // Extract intensity:
      icecream >> double_imanip() >> mi0;

      // Extract reference temperature for Intensity in K:
      icecream >> double_imanip() >> mti0;

      // Extract lower state energy:
      icecream >> double_imanip() >> melow;

      // Extract Einstein A-coefficient:
      icecream >> double_imanip() >> ma;

      // Extract upper state stat. weight:
      icecream >> double_imanip() >> mgupper;

      // Extract lower state stat. weight:
      icecream >> double_imanip() >> mglower;

      String token;
      Index nelem;
      icecream >> token;

      while (icecream) {
        // Read pressure broadening (LEGACY)
        if (token == "PB") {
          mstandard = true;
          LineShape::from_pressurebroadeningdata(
              icecream, mlineshapemodel, mqid);
          icecream >> token;
        } else if (token == "QN") {
          // Quantum numbers

          icecream >> token;
          if (token != "UP") {
            ostringstream os;
            os << "Unknown quantum number tag: " << token;
            throw std::runtime_error(os.str());
          }

          icecream >> token;
          Rational r;
          while (icecream) {
            ThrowIfQuantumNumberNameInvalid(token);
            icecream >> r;
            mqid.UpperQuantumNumbers().Set(token, r);
            icecream >> token;
            if (token == "LO") break;
          }

          if (!is || token != "LO") {
            std::ostringstream os;
            os << "Error in catalog. Lower quantum number tag 'LO' not found.";
            throw std::runtime_error(os.str());
          }

          icecream >> token;
          while (icecream && IsValidQuantumNumberName(token)) {
            icecream >> r;
            mqid.LowerQuantumNumbers().Set(token, r);
            icecream >> token;
          }
        } else if (token == "LM")  // LEGACY
        {
          LineShape::from_linemixingdata(icecream, line_mixing_model);
          icecream >> token;
          lmd_found = true;
        } else if (token == "LF") {
          mstandard = true;
          LineShape::from_linefunctiondata(icecream, mlineshapemodel);
          icecream >> token;
        } else if (token == "LS") {
          mstandard = true;
          icecream >> mlineshapemodel;
          icecream >> token;
        } else if (token == "ZM") {
          // Zeeman effect
          icecream >> mzeemanmodel;
          icecream >> token;
        } else if (token == "LSM") {
          // Line shape modifications

          // Starts with the number of modifications
          icecream >> nelem;
          for (Index lsm = 0; lsm < nelem; lsm++) {
            icecream >> token;

            // cutoff frequency
            if (token == "CUT") {
              Numeric value = NAN;
              icecream >> double_imanip() >> value;
              mcutoff = value;
            }

            // linemixing pressure limit
            if (token == "LML") {
              Numeric value = NAN;
              icecream >> double_imanip() >> value;
              mlinemixing_limit = value;
            }

            // mirroring
            else if (token == "MTM") {
              String value;
              icecream >> value;

              SetMirroringType(MirroringTypeFromString(value));
            }

            // line normalization
            else if (token == "LNT") {
              String value;
              icecream >> value;

              SetLineNormalizationType(LineNormalizationTypeFromString(value));
            }

            else {
              ostringstream os;
              os << "Unknown line modifications given: " << token;
              throw std::runtime_error(os.str());
            }
          }
          icecream >> token;
        } else {
          ostringstream os;
          os << "Unknown line data tag: " << token;
          throw std::runtime_error(os.str());
        }
      }
    }
  } catch (const std::runtime_error& e) {
    ostringstream os;
    os << "Parse error in catalog line: " << endl;
    os << line << endl;
    os << e.what();
    throw std::runtime_error(os.str());
  }

  if (lmd_found)
    mlineshapemodel.SetLineMixingModel(line_mixing_model.Data()[0]);

  // That's it!
  return false;
}

ostream& operator<<(ostream& os, const LineRecord& lr) {
  // Determine the precision, depending on whether Numeric is double
  // or float:
  int precision;
#ifdef USE_FLOAT
  precision = FLT_DIG;
#else
#ifdef USE_DOUBLE
  precision = DBL_DIG;
#else
#error Numeric must be double or float
#endif
#endif

  ostringstream ls;

  switch (lr.Version()) {
    case 5:
      ls << "@"
         << " " << lr.Name() << " " << setprecision(precision) << lr.F() << " "
         << lr.I0() << " " << lr.Ti0() << " " << lr.Elow() << " " << lr.A()
         << " " << lr.G_upper() << " " << lr.G_lower();

      // Write Pressure Broadening and Line Mixing
      { ls << " LS " << lr.GetLineShapeModel(); }

      // Write Quantum Numbers
      {
        Index nUpper = lr.UpperQuantumNumbers().nNumbers();
        Index nLower = lr.LowerQuantumNumbers().nNumbers();

        if (nUpper || nLower) {
          ls << " QN";

          if (nUpper) ls << " UP " << lr.UpperQuantumNumbers();

          if (nLower) ls << " LO " << lr.LowerQuantumNumbers();
        }
      }

      // Write Zeeman Effect Data
      ls << " ZM " << lr.ZeemanModel();

      // Line shape modifications
      {
        const Numeric CUT = lr.CutOff();
        const Numeric LML = lr.LineMixingLimit();
        const Index MTM = (Index)lr.GetMirroringType();
        const Index LNT = (Index)lr.GetLineNormalizationType();

        const bool need_cut = CUT > 0, need_lml = not(LML < 0), need_mtm = MTM,
                   need_lnt = LNT;

        const Index nelem = (Index)need_cut + (Index)need_lml +
                            (Index)need_mtm + (Index)need_lnt;

        if (nelem) {
          ls << " LSM " << nelem;
          if (need_cut) ls << " CUT " << CUT;
          if (need_lml) ls << " LML " << LML;
          if (need_mtm) ls << " MTM " << lr.GetMirroringTypeString();
          if (need_lnt) ls << " LNT " << lr.GetLineNormalizationTypeString();
        }
      }

      os << ls.str();

      break;

    default:
      os << "Unknown ARTSCAT version: " << lr.Version();
      break;
  }

  return os;
}

MirroringType MirroringTypeFromString(const String& in) {
  MirroringType mmirroring;
  if (in == "NONE")
    mmirroring = MirroringType::None;
  else if (in == "LP")
    mmirroring = MirroringType::Lorentz;
  else if (in == "SAME")
    mmirroring = MirroringType::SameAsLineShape;
  else if (in == "MAN")
    mmirroring = MirroringType::Manual;
  else
    throw std::runtime_error("Cannot recognize the mirroring type");
  return mmirroring;
}

String LineRecord::GetMirroringTypeString() const {
  switch (mmirroring) {
    case MirroringType::None:
      return "NONE";
    case MirroringType::Lorentz:
      return "LP";
    case MirroringType::SameAsLineShape:
      return "SAME";
    case MirroringType::Manual:
      return "MAN";
  }
  throw std::runtime_error(
      "Developer bug:  Add new population types to GetMirroringTypeString");
}

LineNormalizationType LineNormalizationTypeFromString(const String& in) {
  LineNormalizationType mlinenorm;
  if (in == "NONE")
    mlinenorm = LineNormalizationType::None;
  else if (in == "VVH")
    mlinenorm = LineNormalizationType::VVH;
  else if (in == "VVW")
    mlinenorm = LineNormalizationType::VVW;
  else if (in == "RQ")
    mlinenorm = LineNormalizationType::RosenkranzQuadratic;
  else
    throw std::runtime_error("Cannot recognize the normalization type");
  return mlinenorm;
}

String LineRecord::GetLineNormalizationTypeString() const {
  switch (mlinenorm) {
    case LineNormalizationType::None:
      return "NONE";
    case LineNormalizationType::VVH:
      return "VVH";
    case LineNormalizationType::VVW:
      return "VVW";
    case LineNormalizationType::RosenkranzQuadratic:
      return "RQ";
  }
  throw std::runtime_error(
      "Developer bug:  Add new population types to GetLineNormalizationTypeString");
}

LinePopulationType LinePopulationTypeFromString(const String& in) {
  LinePopulationType mpopulation;
  if (in == "LTE")
    mpopulation = LinePopulationType::ByLTE;
  else if (in == "TV")
    mpopulation = LinePopulationType::ByVibrationalTemperatures;
  else if (in == "ND")
    mpopulation = LinePopulationType::ByPopulationDistribution;
  else
    throw std::runtime_error("Cannot recognize the population type");
  return mpopulation;
}

String LineRecord::GetLinePopulationTypeString() const {
  switch (mpopulation) {
    case LinePopulationType::ByLTE:
      return "LTE";
    case LinePopulationType::ByVibrationalTemperatures:
      return "TV";
    case LinePopulationType::ByPopulationDistribution:
      return "ND";
  }
  throw std::runtime_error(
      "Developer bug:  Add new population types to GetLinePopulationTypeString");
}

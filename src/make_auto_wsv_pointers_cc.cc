/* Copyright (C) 2001 Stefan Buehler <sbuehler@uni-bremen.de>

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

#include "arts.h"
#include "matpackI.h"
#include "array.h"
#include "file.h"
#include "auto_wsv_groups.h"
#include "wsv_aux.h"

int main()
{
  try
    {
      // We need group names and WSV data:
      extern const ArrayOfString wsv_group_names;
      extern const Array<WsvRecord> wsv_data;

      // Initialize:
      define_wsv_group_names();
      define_wsv_data();

      const Index n_wsv = wsv_data.nelem();

      ofstream ofs;
      open_output_file(ofs,"auto_wsv_pointers.cc");

      ofs << "/** \\file  auto_wsv_pointers.cc\n"
	  << "    Defines the smart pointers that are used by\n"
	  << "    the engine to access workspace variables.\n\n"

	  << "    This file was generated automatically by make_auto_wsv_pointers_cc.cc.\n"

	  << "    <b>DO NOT EDIT!</b>\n\n"

	  << "    \\date "
	  << __DATE__ << ", "
	  << __TIME__ << " */\n\n";

      ofs << "#include \"arts.h\"\n"
	  << "//#include \"matpackI.h\"\n"
	  << "#include \"array.h\"\n"
	  << "#include \"auto_wsv_groups.h\"\n"
	  << "#include \"wsv_aux.h\"\n"
	  << "#include \"auto_wsv.h\"\n\n";

      ofs << "/** The array of WSV pointers.\n"
	  << "    This can be used to access a WSV by its index. */\n"
	  << "Array<WsvP*> wsv_pointers;\n\n";
      
      ofs << "void define_wsv_pointers(Array<WsvP*>&    wsv_pointers,\n"
	  << "                         WorkSpace&       workspace)\n"
	  << "{\n\n";

      // Now write the pointers one by one:
      for (Index i=0; i<n_wsv; ++i)
	{
	  const WsvRecord& wr = wsv_data[i];

	  ofs << "  {\n"
	      << "    static WsvPointer<"
	      << wsv_group_names[wr.Group()]
	      << "> p(&workspace."
	      << wr.Name()
	      << ");\n";

	  ofs << "    wsv_pointers.push_back(&p);\n"
	      << "  }\n\n";
	}

      ofs << "};\n";

    }
  catch (runtime_error x)
    {
      cout << "Something went wrong. Message text:\n";
      cout << x.what() << '\n';
      return 1;
    }

  return 0;
}

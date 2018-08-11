# -*- coding: utf-8 -*-

# Copyright 2017-2018 PaGMO development team
#
# This file is part of the PaGMO library.
#
# The PaGMO library is free software; you can redistribute it and/or modify
# it under the terms of either:
#
#   * the GNU Lesser General Public License as published by the Free
#     Software Foundation; either version 3 of the License, or (at your
#     option) any later version.
#
# or
#
#   * the GNU General Public License as published by the Free Software
#     Foundation; either version 3 of the License, or (at your option) any
#     later version.
#
# or both in parallel, as here.
#
# The PaGMO library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received copies of the GNU General Public License and the
# GNU Lesser General Public License along with the PaGMO library.  If not,
# see https://www.gnu.org/licenses/.

# for python 2.0 compatibility
from __future__ import absolute_import as _ai

from .core import island


def _island_extract(self, t):
    """Extract user-defined island instance.

    If *t* is the same type of the user-defined island used to construct this island, then a reference to
    the internal user-defined island will be returned. Otherwise, ``None`` will be returned.

    Args:
        t (``type``): the type of the user-defined island to extract

    Returns:
        a reference to the internal user-defined island if it is of type *t*, or ``None`` otherwise

    Raises:
        TypeError: if *t* is not a type

    """
    if not isinstance(t, type):
        raise TypeError("the 't' parameter must be a type")
    if hasattr(t, "_pygmo_cpp_island"):
        return self._cpp_extract(t())
    return self._py_extract(t)


def _island_is(self, t):
    """Check the type of the user-defined island instance.

    If *t* is the same type of the user-defined island used to construct this island, then ``True`` will be
    returned. Otherwise, ``False`` will be returned.

    Args:
        t (``type``): the type of the user-defined island to extract

    Returns:
        ``bool``: whether the user-defined island is of type *t* or not

    Raises:
        TypeError: if *t* is not a type

    """
    return not self.extract(t) is None


# Do the actual patching.
setattr(island, "extract", _island_extract)
setattr(island, "is_", _island_is)

/**
 * @file   dimension.h
 *
 * @section LICENSE
 *
 * The MIT License
 *
 * @copyright Copyright (c) 2017-2018 TileDB, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * @section DESCRIPTION
 *
 * This file defines class Dimension.
 */

#ifndef TILEDB_DIMENSION_H
#define TILEDB_DIMENSION_H

#include <string>

#include "tiledb/sm/buffer/buffer.h"
#include "tiledb/sm/enums/compressor.h"
#include "tiledb/sm/enums/datatype.h"
#include "tiledb/sm/misc/status.h"

namespace tiledb {
namespace sm {

/** Manipulates a TileDB dimension. */
class Dimension {
 public:
  /* ********************************* */
  /*     CONSTRUCTORS & DESTRUCTORS    */
  /* ********************************* */

  /** Constructor. */
  Dimension();

  /**
   * Constructor.
   *
   * @param name The name of the dimension.
   * @param type The type of the dimension.
   */
  Dimension(const std::string& name, Datatype type);

  /**
   * Constructor. It clones the input.
   *
   * @param dim The dimension to clone.
   */
  explicit Dimension(const Dimension* dim);

  /** Destructor. */
  ~Dimension();

  /* ********************************* */
  /*                API                */
  /* ********************************* */

  /**
   * Populates the object members from the data in the input binary buffer.
   *
   * @param buff The buffer to deserialize from.
   * @param type The type of the dimension.
   * @return Status
   */
  Status deserialize(ConstBuffer* buff, Datatype type);

  /** Returns the domain. */
  void* domain() const;

  /** Dumps the dimension contents in ASCII form in the selected output. */
  void dump(FILE* out) const;

  /** Returns the dimension name. */
  const std::string& name() const;

  /** Returns true if this is an anonymous (unlabled) dimension **/
  bool is_anonymous() const;

  /**
   * Serializes the object members into a binary buffer.
   *
   * @param buff The buffer to serialize the data into.
   * @return Status
   */
  Status serialize(Buffer* buff);

  /** Sets the domain. */
  Status set_domain(const void* domain);

  /** Sets the tile extent. */
  Status set_tile_extent(const void* tile_extent);

  /**
   * If the tile extent is `null`, this function sets the
   * the tile extent to the dimension domain range.
   */
  Status set_null_tile_extent_to_range();

  /**
   * If the tile extent is `null`, this function sets the
   * the tile extent to the dimension domain range.
   *
   * @tparam T The dimension type.
   */
  template <class T>
  Status set_null_tile_extent_to_range();

  /** Returns the tile extent. */
  void* tile_extent() const;

  /** Returns the dimension type. */
  Datatype type() const;

 private:
  /* ********************************* */
  /*         PRIVATE ATTRIBUTES        */
  /* ********************************* */

  /** The dimension domain. */
  void* domain_;

  /** The dimension name. */
  std::string name_;

  /** The tile extent of the dimension. */
  void* tile_extent_;

  /** The dimension type. */
  Datatype type_;

  /* ********************************* */
  /*          PRIVATE METHODS          */
  /* ********************************* */

  /** Returns an error if the set domain is invalid. */
  Status check_domain() const;

  /**
   * Returns an error if the set domain is invalid.
   *
   * @tparam T The type of the dimension domain.
   * @return Status
   */
  template <class T>
  Status check_domain() const;

  /** Returns an error if the set tile extent is invalid. */
  Status check_tile_extent() const;

  /**
   * Returns an error if the set tile extent is invalid.
   *
   * @tparam T The type of the dimension domain.
   * @return Status
   */
  template <class T>
  Status check_tile_extent() const;
};

}  // namespace sm
}  // namespace tiledb

#endif  // TILEDB_DIMENSION_H

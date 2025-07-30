/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef BINARY_SERIALIZER_HPP
#define BINARY_SERIALIZER_HPP

#include "Logger.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

/**
 * Fast, header-only binary serialization system using smart pointers internally
 * Designed for high performance game save/load operations
 */
namespace BinarySerial {

/**
 * Main binary writer class using smart pointers for memory management
 */
class Writer {
private:
  std::shared_ptr<std::ostream> m_stream;

public:
  explicit Writer(std::shared_ptr<std::ostream> stream) : m_stream(stream) {
    if (!stream || !stream->good()) {
      throw std::runtime_error("Invalid output stream");
    }
  }

  ~Writer() {
    if (m_stream) {
      m_stream->flush();
    }
  }

  // Create writer for file
  static std::unique_ptr<Writer> createFileWriter(const std::string &filename) {
    auto stream = std::make_shared<std::ofstream>(filename, std::ios::binary);
    if (!stream->is_open()) {
      SAVEGAME_ERROR("Failed to create writer for file: " + filename);
      return nullptr;
    }
    SAVEGAME_DEBUG("Created binary writer for file: " + filename);
    return std::make_unique<Writer>(stream);
  }

  // Write fundamental types
  template <typename T> bool write(const T &value) {
    static_assert(std::is_trivially_copyable_v<T>,
                  "Type must be trivially copyable");
    m_stream->write(reinterpret_cast<const char *>(&value), sizeof(T));
    return m_stream->good();
  }

  // Write strings
  bool writeString(const std::string &str) {
    uint32_t length = static_cast<uint32_t>(str.length());
    if (!write(length)) {
      return false;
    }
    if (length > 0) {
      m_stream->write(str.c_str(), length);
    }
    return m_stream->good();
  }

  // Write vectors of trivially copyable types
  template <typename T> bool writeVector(const std::vector<T> &vec) {
    static_assert(std::is_trivially_copyable_v<T>,
                  "Type must be trivially copyable");
    uint32_t size = static_cast<uint32_t>(vec.size());
    if (!write(size)) {
      return false;
    }
    if (size > 0) {
      m_stream->write(reinterpret_cast<const char *>(vec.data()),
                      sizeof(T) * size);
    }
    return m_stream->good();
  }

  // Write custom serializable objects
  template <typename T> bool writeSerializable(const T &obj) {
    return obj.serialize(*m_stream);
  }

  bool good() const { return m_stream && m_stream->good(); }

  void flush() {
    if (m_stream) {
      m_stream->flush();
    }
  }
};

/**
 * Main binary reader class using smart pointers for memory management
 */
class Reader {
private:
  std::shared_ptr<std::istream> m_stream;

public:
  explicit Reader(std::shared_ptr<std::istream> stream) : m_stream(stream) {
    if (!stream || !stream->good()) {
      throw std::runtime_error("Invalid input stream");
    }
  }

  // Create reader for file
  static std::unique_ptr<Reader> createFileReader(const std::string &filename) {
    auto stream = std::make_shared<std::ifstream>(filename, std::ios::binary);
    if (!stream->is_open()) {
      SAVEGAME_ERROR("Failed to create reader for file: " + filename);
      return nullptr;
    }
    SAVEGAME_DEBUG("Created binary reader for file: " + filename);
    return std::make_unique<Reader>(stream);
  }

  // Read fundamental types
  template <typename T> bool read(T &value) {
    static_assert(std::is_trivially_copyable_v<T>,
                  "Type must be trivially copyable");
    m_stream->read(reinterpret_cast<char *>(&value), sizeof(T));
    return m_stream->good() && m_stream->gcount() == sizeof(T);
  }

  // Read strings
  bool readString(std::string &str) {
    uint32_t length = 0;
    if (!read(length)) {
      return false;
    }

    if (length == 0) {
      str.clear();
      return true;
    }

    // Safety check for reasonable string length
    if (length > 1024 * 1024) { // 1MB limit
      SAVEGAME_ERROR("String length too large: " + std::to_string(length) +
                     " bytes");
      return false;
    }

    str.resize(length);
    m_stream->read(&str[0], length);
    return m_stream->good() &&
           m_stream->gcount() == static_cast<std::streamsize>(length);
  }

  // Read vectors of trivially copyable types
  template <typename T> bool readVector(std::vector<T> &vec) {
    static_assert(std::is_trivially_copyable_v<T>,
                  "Type must be trivially copyable");
    uint32_t size = 0;
    if (!read(size)) {
      return false;
    }

    if (size == 0) {
      vec.clear();
      return true;
    }

    // Safety check for reasonable vector size
    if (size > 1024 * 1024) { // 1M elements limit
      SAVEGAME_ERROR("Vector size too large: " + std::to_string(size) +
                     " elements");
      return false;
    }

    vec.resize(size);
    m_stream->read(reinterpret_cast<char *>(vec.data()), sizeof(T) * size);
    return m_stream->good() &&
           m_stream->gcount() == static_cast<std::streamsize>(sizeof(T) * size);
  }

  // Read custom serializable objects
  template <typename T> bool readSerializable(T &obj) {
    return obj.deserialize(*m_stream);
  }

  bool good() const { return m_stream && m_stream->good(); }
};

/**
 * Convenience functions for simple save/load operations
 */
template <typename T>
bool saveToFile(const std::string &filename, const T &object) {
  SAVEGAME_DEBUG("Saving object to file: " + filename);
  auto writer = Writer::createFileWriter(filename);
  if (!writer) {
    return false;
  }

  bool result = writer->writeSerializable(object);
  writer->flush();

  if (result && writer->good()) {
    SAVEGAME_INFO("Successfully saved object to file: " + filename);
  } else {
    SAVEGAME_ERROR("Failed to save object to file: " + filename);
  }

  return result && writer->good();
}

template <typename T>
bool loadFromFile(const std::string &filename, T &object) {
  SAVEGAME_DEBUG("Loading object from file: " + filename);
  auto reader = Reader::createFileReader(filename);
  if (!reader) {
    return false;
  }

  bool result = reader->readSerializable(object) && reader->good();

  if (result) {
    SAVEGAME_INFO("Successfully loaded object from file: " + filename);
  } else {
    SAVEGAME_ERROR("Failed to load object from file: " + filename);
  }

  return result;
}

} // namespace BinarySerial

/**
 * Interface for serializable objects
 */
class ISerializable {
public:
  virtual ~ISerializable() = default;
  virtual bool serialize(std::ostream &stream) const = 0;
  virtual bool deserialize(std::istream &stream) = 0;
};

/**
 * Helper macros for easy implementation of serializable classes
 */
#define DECLARE_SERIALIZABLE()                                                 \
  bool serialize(std::ostream &stream) const override;                         \
  bool deserialize(std::istream &stream) override;

#define BEGIN_SERIALIZE(className)                                             \
  bool className::serialize(std::ostream &stream) const {                      \
    try {

#define END_SERIALIZE()                                                        \
  return true;                                                                 \
  }                                                                            \
  catch (const std::exception &) {                                             \
    return false;                                                              \
  }                                                                            \
  }

#define BEGIN_DESERIALIZE(className)                                           \
  bool className::deserialize(std::istream &stream) {                          \
    try {

#define END_DESERIALIZE()                                                      \
  return true;                                                                 \
  }                                                                            \
  catch (const std::exception &) {                                             \
    return false;                                                              \
  }                                                                            \
  }

#define SERIALIZE_PRIMITIVE(writer, member)                                    \
  if (!writer.write(member))                                                   \
    return false;

#define DESERIALIZE_PRIMITIVE(reader, member)                                  \
  if (!reader.read(member))                                                    \
    return false;

#define SERIALIZE_STRING(writer, member)                                       \
  if (!writer.writeString(member))                                             \
    return false;

#define DESERIALIZE_STRING(reader, member)                                     \
  if (!reader.readString(member))                                              \
    return false;

#define SERIALIZE_SERIALIZABLE(writer, member)                                 \
  if (!writer.writeSerializable(member))                                       \
    return false;

#define DESERIALIZE_SERIALIZABLE(reader, member)                               \
  if (!reader.readSerializable(member))                                        \
    return false;

#endif // BINARY_SERIALIZER_HPP
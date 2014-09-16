#include "../../testing/testing.hpp"

#include "../file_container.hpp"
#include "../varint.hpp"

#include "../../base/logging.hpp"
#include "../../base/string_utils.hpp"


UNIT_TEST(FilesContainer_Smoke)
{
  string const fName = "file_container.tmp";
  FileWriter::DeleteFileX(fName);
  size_t const count = 10;

  // fill container one by one
  {
    FilesContainerW writer(fName);

    for (size_t i = 0; i < count; ++i)
    {
      FileWriter w = writer.GetWriter(strings::to_string(i));

      for (uint32_t j = 0; j < i; ++j)
        WriteVarUint(w, j);
    }
  }

  // read container one by one
  {
    FilesContainerR reader(fName);

    for (size_t i = 0; i < count; ++i)
    {
      FilesContainerR::ReaderT r = reader.GetReader(strings::to_string(i));
      ReaderSource<FilesContainerR::ReaderT> src(r);

      for (uint32_t j = 0; j < i; ++j)
      {
        uint32_t const test = ReadVarUint<uint32_t>(src);
        TEST_EQUAL(j, test, ());
      }
    }
  }

  // append to container
  uint32_t const arrAppend[] = { 888, 777, 666 };
  for (size_t i = 0; i < ARRAY_SIZE(arrAppend); ++i)
  {
    {
      FilesContainerW writer(fName, FileWriter::OP_WRITE_EXISTING);

      FileWriter w = writer.GetWriter(strings::to_string(arrAppend[i]));
      WriteVarUint(w, arrAppend[i]);
    }

    // read appended
    {
      FilesContainerR reader(fName);

      FilesContainerR::ReaderT r = reader.GetReader(strings::to_string(arrAppend[i]));
      ReaderSource<FilesContainerR::ReaderT> src(r);

      uint32_t const test = ReadVarUint<uint32_t>(src);
      TEST_EQUAL(arrAppend[i], test, ());
    }
  }
  FileWriter::DeleteFileX(fName);
}

namespace
{
  void CheckInvariant(FilesContainerR & reader, string const & tag, int64_t test)
  {
    FilesContainerR::ReaderT r = reader.GetReader(tag);
    TEST_EQUAL(test, ReadPrimitiveFromPos<int64_t>(r, 0), ());
  }
}

UNIT_TEST(FilesContainer_Shared)
{
  string const fName = "file_container.tmp";
  FileWriter::DeleteFileX(fName);

  uint32_t const count = 10;
  int64_t const test64 = 908175281437210836LL;

  {
    // shared container fill

    FilesContainerW writer(fName);

    FileWriter w1 = writer.GetWriter("5");
    WriteToSink(w1, uint32_t(0));

    for (uint32_t i = 0; i < count; ++i)
      WriteVarUint(w1, i);
    w1.Flush();

    FileWriter w2 = writer.GetWriter("2");
    WriteToSink(w2, test64);
    w2.Flush();
  }

  {
    // shared container read and fill

    FilesContainerR reader(fName);
    FilesContainerR::ReaderT r1 = reader.GetReader("5");
    uint64_t const offset = sizeof(uint32_t);
    r1 = r1.SubReader(offset, r1.Size() - offset);

    CheckInvariant(reader, "2", test64);

    FilesContainerW writer(fName, FileWriter::OP_WRITE_EXISTING);
    FileWriter w = writer.GetWriter("3");

    ReaderSource<FilesContainerR::ReaderT> src(r1);
    for (uint32_t i = 0; i < count; ++i)
    {
      uint32_t test = ReadVarUint<uint32_t>(src);
      TEST_EQUAL(test, i, ());
      WriteVarUint(w, i);
    }
  }

  {
    // check invariant
    FilesContainerR reader(fName);
    CheckInvariant(reader, "2", test64);
  }

  FileWriter::DeleteFileX(fName);
}

namespace
{
  void ReplaceInContainer(string const & fName,
                          char const * key, char const * value)
  {
    FilesContainerW writer(fName, FileWriter::OP_WRITE_EXISTING);
    FileWriter w = writer.GetWriter(key);
    w.Write(value, strlen(value));
  }

  void CheckContainer(string const & fName,
                      char const * key[], char const * value[], size_t count)
  {
    FilesContainerR reader(fName);
    LOG(LINFO, ("Size=", reader.GetFileSize()));

    for (size_t i = 0; i < count; ++i)
    {
      FilesContainerR::ReaderT r = reader.GetReader(key[i]);

      size_t const szBuffer = 100;
      size_t const szS = strlen(value[i]);

      char s[szBuffer] = { 0 };
      ASSERT_LESS ( szS, szBuffer, () );
      r.Read(0, s, szS);

      TEST(strcmp(value[i], s) == 0, (s));
    }
  }
}

UNIT_TEST(FilesContainer_RewriteExisting)
{
  string const fName = "file_container.tmp";
  FileWriter::DeleteFileX(fName);

  char const * key[] = { "3", "2", "1" };
  char const * value[] = { "prolog", "data", "epilog" };

  // fill container
  {
    FilesContainerW writer(fName);

    for (size_t i = 0; i < ARRAY_SIZE(key); ++i)
    {
      FileWriter w = writer.GetWriter(key[i]);
      w.Write(value[i], strlen(value[i]));
    }
  }

  // re-write middle file in container
  char const * buffer1 = "xxxxxxx";
  ReplaceInContainer(fName, key[1], buffer1);
  char const * value1[] = { value[0], buffer1, value[2] };
  CheckContainer(fName, key, value1, 3);

  // re-write end file in container
  char const * buffer2 = "yyyyyyyyyyyyyy";
  ReplaceInContainer(fName, key[2], buffer2);
  char const * value2[] = { value[0], buffer1, buffer2 };
  CheckContainer(fName, key, value2, 3);

  // re-write end file in container once again
  char const * buffer3 = "zzz";
  ReplaceInContainer(fName, key[2], buffer3);
  char const * value3[] = { value[0], buffer1, buffer3 };
  CheckContainer(fName, key, value3, 3);

  FileWriter::DeleteFileX(fName);
}

UNIT_TEST(FilesMappingContainer_Smoke)
{
  string const fName = "file_container.tmp";
  char const * key[] = { "3", "2", "1" };
  uint32_t const count = 1000000;

  // fill container
  {
    FilesContainerW writer(fName);

    for (size_t i = 0; i < ARRAY_SIZE(key); ++i)
    {
      FileWriter w = writer.GetWriter(key[i]);
      for (uint32_t j = 0; j < count; ++j)
      {
        uint32_t v = j + i;
        w.Write(&v, sizeof(v));
      }
    }
  }

  {
    FilesMappingContainer reader(fName);
    for (size_t i = 0; i < ARRAY_SIZE(key); ++i)
    {
      FilesMappingContainer::Handle h = reader.Map(key[i]);
      uint32_t const * data = reinterpret_cast<uint32_t const *>(h.GetData());
      for (uint32_t j = 0; j < count; ++j)
      {
        TEST_EQUAL(j + i, *data, ());
        ++data;
      }

      h.Unmap();
    }
  }

  FileWriter::DeleteFileX(fName);
}

UNIT_TEST(FilesMappingContainer_PageSize)
{
  string const fName = "file_container.tmp";

  size_t const pageSize = sysconf(_SC_PAGE_SIZE);
  LOG(LINFO, ("Page size:", pageSize));

  char const * key[] = { "3", "2", "1" };
  char const byte[] = { 'a', 'b', 'c', 'd', 'e', 'f', 'g' };
  size_t count[] = { pageSize-1, pageSize, pageSize+1 };
  size_t const sz = ARRAY_SIZE(key);

  {
    FilesContainerW writer(fName);

    for (size_t i = 0; i < sz; ++i)
    {
      FileWriter w = writer.GetWriter(key[i]);
      for (size_t j = 0; j < count[i]; ++j)
        w.Write(&byte[j % ARRAY_SIZE(byte)], 1);
    }
  }

  {
    FilesMappingContainer reader(fName);
    FilesMappingContainer::Handle handle[sz];

    for (size_t i = 0; i < sz; ++i)
    {
      handle[i] = reader.Map(key[i]);
      TEST_EQUAL(handle[i].GetSize(), count[i], ());
    }

    for (size_t i = 0; i < sz; ++i)
    {
      char const * data = handle[i].GetData();
      for (size_t j = 0; j < count[i]; ++j)
        TEST_EQUAL(*data++, byte[j % ARRAY_SIZE(byte)], ());
    }

    for (size_t i = 0; i < sz; ++i)
      handle[i].Unmap();
  }

  FileWriter::DeleteFileX(fName);
}

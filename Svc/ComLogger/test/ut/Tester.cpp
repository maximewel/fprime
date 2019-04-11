// ----------------------------------------------------------------------
// CommandSequencer/test/ut/Tester.cpp
// ----------------------------------------------------------------------

#include <stdio.h>

#include "Tester.hpp"
#include "Fw/Cmd/CmdPacket.hpp"
#include <Os/ValidateFile.hpp>
#include <Os/FileSystem.hpp>
#include <Fw/Types/SerialBuffer.hpp>

#define ID_BASE 256

namespace Svc {

  Tester ::
    Tester(
        const char *const compName
    ) : 
      ComLoggerGTestBase(compName, 30),
      comLogger("ComLogger", FILE_STR, MAX_BYTES_PER_FILE)
  {
      this->connectPorts();
      this->initComponents();
  }

  Tester ::
    ~Tester(void) 
  {
    
  }

  void Tester ::
    connectPorts(void)
  {
    comLogger.set_cmdRegOut_OutputPort(0, this->get_from_cmdRegOut(0));
    comLogger.set_cmdResponseOut_OutputPort(0, this->get_from_cmdResponseOut(0));
    this->connect_to_cmdIn(0, comLogger.get_cmdIn_InputPort(0));
    this->connect_to_comIn(0, comLogger.get_comIn_InputPort(0));
    comLogger.set_timeCaller_OutputPort(0, this->get_from_timeCaller(0));
    comLogger.set_logOut_OutputPort(0, this->get_from_logOut(0));
  }

  void Tester ::
    initComponents(void) 
  {
    this->init();
    this->comLogger.init(QUEUE_DEPTH, 0);
  }

  void Tester ::
    dispatchOne(void)
  {
    this->comLogger.doDispatch();
  }
  
  void Tester ::
    dispatchAll(void)
  {
    while(this->comLogger.m_queue.getNumMsgs() > 0)
      this->dispatchOne();
  }

  // ----------------------------------------------------------------------
  // Tests 
  // ----------------------------------------------------------------------
  void Tester ::
    testLogging(void) 
  {
      U8 fileName[2048];
      U8 prevFileName[2048];
      U8 hashFileName[2048];
      U8 prevHashFileName[2048];
      U8 buf[1024];
      NATIVE_INT_TYPE length;
      U16 bufferSize = 0;
      Os::File::Status ret;
      Os::File file;

      ASSERT_TRUE(comLogger.fileMode == ComLogger::CLOSED);
      ASSERT_EVENTS_SIZE(0);

      U8 data[COM_BUFFER_LENGTH] = {0xde,0xad,0xbe,0xef};
      Fw::ComBuffer buffer(&data[0], sizeof(data));

      Fw::SerializeStatus stat;
      
      for(int j = 0; j < 3; j++)
      {
        // Test times for the different iterations:
        Fw::Time testTime(TB_NONE, j, 9876543);
        Fw::Time testTimePrev(TB_NONE, j-1, 9876543);
        Fw::Time testTimeNext(TB_NONE, j+1, 9876543);

        // File names for the different iterations:
        memset(fileName, 0, sizeof(fileName));
        snprintf((char*) fileName, sizeof(fileName), "%s_%d_%d_%06d.com", FILE_STR, testTime.getTimeBase(), testTime.getSeconds(), testTime.getUSeconds());
        memset(hashFileName, 0, sizeof(hashFileName));
        snprintf((char*) hashFileName, sizeof(hashFileName), "%s_%d_%d_%06d.com%s", FILE_STR, testTime.getTimeBase(), testTime.getSeconds(), testTime.getUSeconds(), Utils::Hash::getFileExtensionString());
        memset(prevFileName, 0, sizeof(prevFileName));
        snprintf((char*) prevFileName, sizeof(prevFileName), "%s_%d_%d_%06d.com", FILE_STR, testTime.getTimeBase(), testTimePrev.getSeconds(), testTimePrev.getUSeconds());
        memset(prevHashFileName, 0, sizeof(prevHashFileName));
        snprintf((char*) prevHashFileName, sizeof(prevHashFileName), "%s_%d_%d_%06d.com%s", FILE_STR, testTime.getTimeBase(), testTimePrev.getSeconds(), testTimePrev.getUSeconds(), Utils::Hash::getFileExtensionString());

        // Set the test time to the current time:
        setTestTime(testTime);

        // Write to file:
        for(int i = 0; i < MAX_ENTRIES_PER_FILE-1; i++)
        {
          invoke_to_comIn(0, buffer, 0);
          dispatchAll();
          ASSERT_TRUE(comLogger.fileMode == ComLogger::OPEN);
        }

        // OK a new file should be opened after this final invoke, set a new test time so that a file
        // with a new name gets opened:
        setTestTime(testTimeNext);
        invoke_to_comIn(0, buffer, 0);
        dispatchAll();
        ASSERT_TRUE(comLogger.fileMode == ComLogger::OPEN);

        // A new file should have been opened from the previous loop iteration:
        if( j > 0 ) {
          ASSERT_TRUE(comLogger.fileMode == ComLogger::OPEN);
          ASSERT_TRUE(strcmp((char*) comLogger.fileName, (char*) fileName) == 0 );
        }

        // Make sure we got a closed file event:
        ASSERT_EVENTS_SIZE(j);
        ASSERT_EVENTS_FileClosed_SIZE(j);
        if( j > 0 ) {
          ASSERT_EVENTS_FileClosed(j-1, (char*) prevFileName);
        }

        // Make sure the file size is smaller or equal to the limit:
        Os::FileSystem::Status fsStat;
        U64 fileSize = 0;
        fsStat = Os::FileSystem::getFileSize((char*) fileName, fileSize); //!< gets the size of the file (in bytes) at location path
        ASSERT_EQ(fsStat, Os::FileSystem::OP_OK);
        ASSERT_LE(fileSize, MAX_BYTES_PER_FILE);

        // Open file:
        ret = file.open((char*) fileName, Os::File::OPEN_READ);
        ASSERT_EQ(Os::File::OP_OK,ret);

        // Check data:
        for(int i = 0; i < 5; i++)
        {
          // Get length of buffer to read
          NATIVE_INT_TYPE length = sizeof(U16);
          ret = file.read(&buf, length);
          ASSERT_EQ(Os::File::OP_OK, ret);
          ASSERT_EQ(length, (NATIVE_INT_TYPE) sizeof(U16));
          Fw::SerialBuffer comBuffLength(buf, length);
          comBuffLength.fill();
          stat = comBuffLength.deserialize(bufferSize);
          ASSERT_EQ(stat, Fw::FW_SERIALIZE_OK);
          ASSERT_EQ((U16) COM_BUFFER_LENGTH, bufferSize);

          // Read and check buffer:
          length = bufferSize;
          ret = file.read(&buf, length);
          ASSERT_EQ(Os::File::OP_OK,ret);
          ASSERT_EQ(length, (NATIVE_INT_TYPE) bufferSize);
          ASSERT_EQ(memcmp(buf, data, COM_BUFFER_LENGTH), 0);

          //for(int k=0; k < 4; k++)
          //  printf("0x%02x ",buf[k]);
          //printf("\n");
        }

        // Make sure we reached the end of the file:
        length = sizeof(NATIVE_INT_TYPE);
        ret = file.read(&buf, length);
        ASSERT_EQ(Os::File::OP_OK,ret);
        ASSERT_EQ(length, 0);
        file.close();

        // Assert that the hashes match:
        if( j > 0 ) {
          Os::ValidateFile::Status status;
          status = Os::ValidateFile::validate((char*) prevFileName, (char*) prevHashFileName);
          ASSERT_EQ(Os::ValidateFile::VALIDATION_OK, status);
        }
     }
  }

  void Tester ::
    testLoggingNoLength(void) 
  {
      U8 fileName[2048];
      U8 prevFileName[2048];
      U8 hashFileName[2048];
      U8 prevHashFileName[2048];
      U8 buf[1024];
      NATIVE_INT_TYPE length;
      Os::File::Status ret;
      Os::File file;

      ASSERT_TRUE(comLogger.fileMode == ComLogger::CLOSED);
      ASSERT_EVENTS_SIZE(0);

      U8 data[COM_BUFFER_LENGTH] = {0xde,0xad,0xbe,0xef};
      Fw::ComBuffer buffer(&data[0], sizeof(data));

      // Make sure that noLengthMode is enabled:
      comLogger.storeBufferLength = false;
      comLogger.maxFileSize = MAX_BYTES_PER_FILE_NO_LENGTH;
      
      for(int j = 0; j < 3; j++)
      {
        // Test times for the different iterations:
        Fw::Time testTime(TB_NONE, j, 123456);
        Fw::Time testTimePrev(TB_NONE, j-1, 123456);
        Fw::Time testTimeNext(TB_NONE, j+1, 123456);

        // File names for the different iterations:
        memset(fileName, 0, sizeof(fileName));
        snprintf((char*) fileName, sizeof(fileName), "%s_%d_%d_%06d.com", FILE_STR, testTime.getTimeBase(), testTime.getSeconds(), testTime.getUSeconds());
        memset(hashFileName, 0, sizeof(hashFileName));
        snprintf((char*) hashFileName, sizeof(hashFileName), "%s_%d_%d_%06d.com%s", FILE_STR, testTime.getTimeBase(), testTime.getSeconds(), testTime.getUSeconds(), Utils::Hash::getFileExtensionString());
        memset(prevFileName, 0, sizeof(prevFileName));
        snprintf((char*) prevFileName, sizeof(prevFileName), "%s_%d_%d_%06d.com", FILE_STR, testTime.getTimeBase(), testTimePrev.getSeconds(), testTimePrev.getUSeconds());
        memset(prevHashFileName, 0, sizeof(prevHashFileName));
        snprintf((char*) prevHashFileName, sizeof(prevHashFileName), "%s_%d_%d_%06d.com%s", FILE_STR, testTime.getTimeBase(), testTimePrev.getSeconds(), testTimePrev.getUSeconds(), Utils::Hash::getFileExtensionString());

        // Set the test time to the current time:
        setTestTime(testTime);

        // Write to file:
        for(int i = 0; i < MAX_ENTRIES_PER_FILE-1; i++)
        {
          invoke_to_comIn(0, buffer, 0);
          dispatchAll();
          ASSERT_TRUE(comLogger.fileMode == ComLogger::OPEN);
        }

        // OK a new file should be opened after this final invoke, set a new test time so that a file
        // with a new name gets opened:
        setTestTime(testTimeNext);
        invoke_to_comIn(0, buffer, 0);
        dispatchAll();
        ASSERT_TRUE(comLogger.fileMode == ComLogger::OPEN);

        // A new file should have been opened from the previous loop iteration:
        if( j > 0 ) {
          ASSERT_TRUE(comLogger.fileMode == ComLogger::OPEN);
          ASSERT_TRUE(strcmp((char*) comLogger.fileName, (char*) fileName) == 0 );
        }

        // Make sure we got a closed file event:
        ASSERT_EVENTS_SIZE(j);
        ASSERT_EVENTS_FileClosed_SIZE(j);
        if( j > 0 ) {
          ASSERT_EVENTS_FileClosed(j-1, (char*) prevFileName);
        }

        // Make sure the file size is smaller or equal to the limit:
        Os::FileSystem::Status fsStat;
        U64 fileSize = 0;
        fsStat = Os::FileSystem::getFileSize((char*) fileName, fileSize); //!< gets the size of the file (in bytes) at location path
        ASSERT_EQ(fsStat, Os::FileSystem::OP_OK);
        ASSERT_LE(fileSize, MAX_BYTES_PER_FILE);

        // Open file:
        ret = file.open((char*) fileName, Os::File::OPEN_READ);
        ASSERT_EQ(Os::File::OP_OK,ret);

        // Check data:
        for(int i = 0; i < 5; i++)
        {
          // Get length of buffer to read
          NATIVE_INT_TYPE length = COM_BUFFER_LENGTH;
          ret = file.read(&buf, length);
          ASSERT_EQ(Os::File::OP_OK,ret);
          ASSERT_EQ(length, (NATIVE_INT_TYPE) COM_BUFFER_LENGTH);
          ASSERT_EQ(memcmp(buf, data, COM_BUFFER_LENGTH), 0);

          //for(int k=0; k < 4; k++)
          //  printf("0x%02x ",buf[k]);
          //printf("\n");
        }

        // Make sure we reached the end of the file:
        length = sizeof(NATIVE_INT_TYPE);
        ret = file.read(&buf, length);
        ASSERT_EQ(Os::File::OP_OK,ret);
        ASSERT_EQ(length, 0);
        file.close();

        // Assert that the hashes match:
        if( j > 0 ) {
          Os::ValidateFile::Status status;
          status = Os::ValidateFile::validate((char*) prevFileName, (char*) prevHashFileName);
          ASSERT_EQ(Os::ValidateFile::VALIDATION_OK, status);
        }
     }
  }

  void Tester ::
    openError(void) 
  {
      // Construct illegal filePrefix, and set it via the friend:
      U8 filePrefix[2048];
      U8 fileName[2048];
      memset(fileName, 0, sizeof(fileName));
      memset(filePrefix, 0, sizeof(filePrefix));
      snprintf((char*) filePrefix, sizeof(filePrefix), "illegal/fname?;\\*");

      strncpy((char*) comLogger.filePrefix, (char*) filePrefix, sizeof(comLogger.filePrefix));
      
      ASSERT_TRUE(comLogger.fileMode == ComLogger::CLOSED);
      ASSERT_EVENTS_SIZE(0);

      const U8 data[COM_BUFFER_LENGTH] = {0xde,0xad,0xbe,0xef};
      Fw::ComBuffer buffer(data, sizeof(data));
      
      Fw::Time testTime(TB_NONE, 4, 9876543);
      setTestTime(testTime);

      snprintf((char*) fileName, sizeof(fileName), "%s_%d_%d_%06d.com", filePrefix, (U32) testTime.getTimeBase(), testTime.getSeconds(), testTime.getUSeconds());

      for(int i = 0; i < 3; i++)
      {
        invoke_to_comIn(0, buffer, 0);
        dispatchAll();
        ASSERT_TRUE(comLogger.fileMode == ComLogger::CLOSED);
      }

      // Check generated events:
      // We only expect 1 because of the throttle
      ASSERT_EVENTS_SIZE(1);
      ASSERT_EVENTS_FileOpenError_SIZE(1);
      ASSERT_EVENTS_FileOpenError(
        0,
        Os::File::DOESNT_EXIST,
        (char*) fileName
      );

      // Try again with valid file name:
      memset(fileName, 0, sizeof(fileName));
      memset(filePrefix, 0, sizeof(filePrefix));
      snprintf((char*) filePrefix, sizeof(filePrefix), "good_");

      strncpy((char*) comLogger.filePrefix, (char*) filePrefix, sizeof(comLogger.filePrefix));
      
      ASSERT_TRUE(comLogger.fileMode == ComLogger::CLOSED);

      snprintf((char*) fileName, sizeof(fileName), "%s_%d_%d_%06d.com", filePrefix, (U32) testTime.getTimeBase(), testTime.getSeconds(), testTime.getUSeconds());

      for(int i = 0; i < 3; i++)
      {
        invoke_to_comIn(0, buffer, 0);
        dispatchAll();
        ASSERT_TRUE(comLogger.fileMode == ComLogger::OPEN);
      }

      // Check generated events:
      // We only expect 1 because of the throttle
      ASSERT_EVENTS_SIZE(1);
      ASSERT_EVENTS_FileOpenError_SIZE(1);
      comLogger.closeFile();

      // Try again with invalid file name:
      memset(fileName, 0, sizeof(fileName));
      memset(filePrefix, 0, sizeof(filePrefix));
      snprintf((char*) filePrefix, sizeof(filePrefix), "illegal/fname?;\\*");

      strncpy((char*) comLogger.filePrefix, (char*) filePrefix, sizeof(comLogger.filePrefix));
      
      ASSERT_TRUE(comLogger.fileMode == ComLogger::CLOSED);

      snprintf((char*) fileName, sizeof(fileName), "%s_%d_%d_%06d.com", filePrefix, (U32) testTime.getTimeBase(), testTime.getSeconds(), testTime.getUSeconds());

      for(int i = 0; i < 3; i++)
      {
        invoke_to_comIn(0, buffer, 0);
        dispatchAll();
        ASSERT_TRUE(comLogger.fileMode == ComLogger::CLOSED);
      }

      // Check generated events:
      // We only expect 1 extra because of the throttle
      ASSERT_EVENTS_SIZE(3); // extra event here because of the file close
      ASSERT_EVENTS_FileOpenError_SIZE(2);
  }

  void Tester ::
    writeError(void) 
  {
      ASSERT_TRUE(comLogger.fileMode == ComLogger::CLOSED);
      ASSERT_EVENTS_SIZE(0);

      const U8 data[4] = {0xde,0xad,0xbe,0xef};
      Fw::ComBuffer buffer(data, sizeof(data));
      
      Fw::Time testTime(TB_NONE, 5, 9876543);
      setTestTime(testTime);

      for(int i = 0; i < 3; i++)
      {
        invoke_to_comIn(0, buffer, 0);
        dispatchAll();
        ASSERT_TRUE(comLogger.fileMode == ComLogger::OPEN);
      }

      // Force close the file from underneath the component:
      comLogger.file.close();

      // Send 2 packets:
      for(int i = 0; i < 3; i++) {
        invoke_to_comIn(0, buffer, 0);
        dispatchAll();
        ASSERT_TRUE(comLogger.fileMode == ComLogger::OPEN);
      }

      // Construct filename:
      U8 fileName[2048];
      memset(fileName, 0, sizeof(fileName));
      snprintf((char*) fileName, sizeof(fileName), "%s_%d_%d_%06d.com", FILE_STR, (U32) testTime.getTimeBase(), testTime.getSeconds(), testTime.getUSeconds());  

      // Check generated events:
      // We should only see a single event because write
      // errors are throttled.
      ASSERT_EVENTS_SIZE(1);
      ASSERT_EVENTS_FileWriteError_SIZE(1);
      ASSERT_EVENTS_FileWriteError(
          0,
          Os::File::NOT_OPENED,
          0,
          2,
          (char*) fileName
      );

      // Make comlogger open a new file:
      comLogger.fileMode = ComLogger::CLOSED;
      comLogger.openFile();

      // Try to write and make sure it succeeds:
      // Send 2 packets:
      for(int i = 0; i < 3; i++) {
        invoke_to_comIn(0, buffer, 0);
        dispatchAll();
        ASSERT_TRUE(comLogger.fileMode == ComLogger::OPEN);
      }

      // Expect no new errors:
      ASSERT_EVENTS_SIZE(1);
      ASSERT_EVENTS_FileWriteError_SIZE(1);

      // Force close the file from underneath the component:
      comLogger.file.close();

      // Send 2 packets:
      for(int i = 0; i < 3; i++) {
        invoke_to_comIn(0, buffer, 0);
        dispatchAll();
        ASSERT_TRUE(comLogger.fileMode == ComLogger::OPEN);
      }

      // Check generated events:
      // We should only see a single event because write
      // errors are throttled.
      ASSERT_EVENTS_SIZE(2);
      ASSERT_EVENTS_FileWriteError_SIZE(2);
      ASSERT_EVENTS_FileWriteError(
          1,
          Os::File::NOT_OPENED,
          0,
          2,
          (char*) fileName
      );
  }

  void Tester ::
    closeFileCommand(void) 
  {
    Os::File file;
    U8 fileName[2048];
    U8 hashFileName[2048];
    Os::File::Status ret;

    // Form filenames:
    Fw::Time testTime(TB_NONE, 6, 9876543);
    setTestTime(testTime);
    memset(fileName, 0, sizeof(fileName));
    snprintf((char*) fileName, sizeof(fileName), "%s_%d_%d_%06d.com", FILE_STR, testTime.getTimeBase(), testTime.getSeconds(), testTime.getUSeconds());
    memset(hashFileName, 0, sizeof(hashFileName));
    snprintf((char*) hashFileName, sizeof(hashFileName), "%s_%d_%d_%06d.com%s", FILE_STR, testTime.getTimeBase(), testTime.getSeconds(), testTime.getUSeconds(), Utils::Hash::getFileExtensionString());

    ASSERT_TRUE(comLogger.fileMode == ComLogger::CLOSED);
    ASSERT_EVENTS_SIZE(0);

    // Send close file commands:
    for(int i = 0; i < 3; i++) {
      sendCmd_CloseFile(0, i+1);
      dispatchAll();
      ASSERT_TRUE(comLogger.fileMode == ComLogger::CLOSED);
    }

    ASSERT_CMD_RESPONSE_SIZE(3);

    for(int i = 0; i < 3; i++) {
      ASSERT_CMD_RESPONSE(
          i,
          ComLogger::OPCODE_CLOSEFILE,
          i+1,
          Fw::COMMAND_OK
      );
    }
    
    const U8 data[COM_BUFFER_LENGTH] = {0xde,0xad,0xbe,0xef};
    Fw::ComBuffer buffer(data, sizeof(data));

    for(int i = 0; i < 3; i++)
    {
      invoke_to_comIn(0, buffer, 0);
      dispatchAll();
      ASSERT_TRUE(comLogger.fileMode == ComLogger::OPEN);
    }

     // Send close file commands:
    for(int i = 0; i < 3; i++) {
      sendCmd_CloseFile(0, i+1);
      dispatchAll();
      ASSERT_TRUE(comLogger.fileMode == ComLogger::CLOSED);
    }

    ASSERT_CMD_RESPONSE_SIZE(6);

    for(int i = 0; i < 3; i++) {
      ASSERT_CMD_RESPONSE(
          i,
          ComLogger::OPCODE_CLOSEFILE,
          i+1,
          Fw::COMMAND_OK
      );
    }

    // Make sure we got a closed file event:
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_FileClosed_SIZE(1);
    ASSERT_EVENTS_FileClosed(0, (char*) fileName);

    // Open files to make sure they exist:
    ret = file.open((char*) fileName, Os::File::OPEN_READ);
    ASSERT_EQ(Os::File::OP_OK,ret);
    file.close();
    ret = file.open((char*) hashFileName, Os::File::OPEN_READ);
    ASSERT_EQ(Os::File::OP_OK,ret);
    file.close();
  }

  void Tester ::
    from_pingOut_handler(
        const NATIVE_INT_TYPE portNum,
        U32 key
    )
  {
    this->pushFromPortEntry_pingOut(key);
  }
};

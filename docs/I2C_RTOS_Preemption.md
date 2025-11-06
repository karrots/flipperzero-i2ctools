# **Agent Best Practice: Diagnosing I2C Failures in Multitasking RTOS (Flipper Zero)**

## **1\. Symptom Pattern Recognition**

When diagnosing I2C issues on a multitasking RTOS like Furi OS, be alert for this specific pattern:

* **Symptom**: ATCA\_RX\_FAIL (-26) or similar CRC/reception errors.  
* **Context**: Operations fail from one context (e.g., CLI task) but work perfectly from another (e.g., GUI thread).  
* **Operation Specific**: The failure is often specific to **READ** operations (e.g., atcab\_read\_config\_zone) while other operations (e.g., atcab\_random, I2C scans) may succeed.

## **2\. Root Cause Analysis: Task Preemption**

This pattern is a classic symptom of **I2C resource contention caused by task preemption**, not a simple timing issue or hardware fault.

### **Failure Mechanism**

1. **Split Transactions**: A library function (e.g., atcab\_read\_config\_zone) is rarely atomic. It executes as multiple, separate I2C transactions separated by a delay.  
   1. **Transaction 1**: hal\_i2c\_send() (Send "read" command).  
   2. **Delay**: atca\_delay\_ms() (Wait for device $T\_{\\text{EXEC}}$).  
   3. **Transaction 3**: hal\_i2c\_receive() (Read data back).  
2. **Preemption Window**: The vulnerability is during the atca\_delay\_ms(). The RTOS scheduler sees the task is idle and **preempts it**.  
3. **Race Condition**: Another system task (e.g., GUI, power poller) acquires the I2C bus mutex and performs its *own* I2C transaction.  
4. **Device Desync**: The ATECC device receives an unexpected command (from the interrupting task) while it was preparing data for the original task.  
5. **Failure**: The original task resumes, attempts its read, and receives garbage data (or nothing), causing a CRC failure (ATCA\_RX\_FAIL).

## **3\. Recommended Solution: Application-Level Mutex**

Do not attempt to fix this by simply increasing delays. The correct architectural solution is to **wrap the entire command execution sequence in an-application level mutex**.

This ensures the *entire* logical operation (Wake \-\> Command \-\> Wait \-\> Read \-\> Sleep) is atomic and cannot be interrupted by another task trying to use the same I2C bus.

### **Example Implementation**

A shared FuriMutex\* (e.g., crypto\_i2c\_mutex) should be used to guard the session execution function.

bool crypto\_session\_execute\_with\_retry(  
    CryptoCommandCallback command,  
    void\* context,  
    CryptoDevicePowerState end\_state,  
    ATCA\_STATUS\* out\_status) {

    furi\_assert(crypto\_i2c\_mutex); // Ensure mutex is initialized

    // Acquire lock BEFORE the entire operation  
    if(furi\_mutex\_acquire(crypto\_i2c\_mutex, FuriWaitForever) \!= FuriStatusOk) {  
        \*out\_status \= ATCA\_FUNC\_FAIL;  
        return false;  
    }

    bool success \= false;  
    ATCA\_STATUS status;

    // ... loop with crypto\_session\_begin(), command(), crypto\_session\_end() ...  
      
    for(int attempt \= 0; attempt \< MAX\_RETRY\_ATTEMPTS; attempt++) {  
        // ...  
        // ... session begin ...  
        // ...  
        status \= command(context);  
        // ...  
        // ... session end ...  
        // ...  
        if(status \== ATCA\_SUCCESS) {  
            success \= true;  
            break;  
        }  
        // ... retry logic ...  
    }

    \*out\_status \= status;

    // RELEASE lock AFTER the entire operation is complete  
    furi\_mutex\_release(crypto\_i2c\_mutex);

    return success;  
}


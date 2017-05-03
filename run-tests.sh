# A convenience wrapper around ctest

# Remove SHM regions
function drop_shm()                                                                 
{                                                                                   
    echo "Dropping SHM entries"                                                     
                                                                                    
    for i in $(ipcs -m | awk '{ print $1; }'); do                                   
        if [[ $i =~ 0x.* ]]; then                                                   
            sudo ipcrm -M $i 2>/dev/null                                            
        fi                                                                          
    done                                                                            
}

drop_shm
sudo ctest

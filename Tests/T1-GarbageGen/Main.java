import java.lang.Exception;

class Garbage {
  String[] myStringArray = new String[4096];
}

public class Main {
  public static void main(String[] args) throws Exception {
    for(;;) { 
      Thread.sleep(10); 
      new Garbage(); 
    }
  }
}

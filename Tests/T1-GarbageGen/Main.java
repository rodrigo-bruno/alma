
class Garbage {
  String[] myStringArray = new String[128];
}

public class Main {
  public static void main(String[] args) {
    for(;;) { new Garbage(); }
  }
}

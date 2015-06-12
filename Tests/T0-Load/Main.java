public class Main {
  public static void main(String[] args) {
    System.out.println("Hello World!");
    Thread t = new Thread();
    int a = countInstances(Thread.class);
    System.out.println("There are " + a + " instances of " + Thread.class);
  }
 
  private static native int countInstances(Class klass);
}
